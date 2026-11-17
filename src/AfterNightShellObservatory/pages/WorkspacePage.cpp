// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/WorkspacePage.h"

#include "chrome/ToolbarClearance.h"
#include "pages/FloatingImagesPage.h"

#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "controls/ImageViewControl.h"
#include "views/ViewChromeBinder.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace an::ui::observatory
{

WorkspacePage::WorkspacePage(KDDockWidgets::QtWidgets::MainWindow& dockArea, FloatingImagesPage& floatingImages,
                             QWidget* parent)
    : QWidget(parent), m_dockArea(&dockArea), m_floatingImages(&floatingImages)
{
    setObjectName(QStringLiteral("observatoryWorkspacePage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    m_stack->addWidget(m_dockArea);
    m_maximizedPage = new QWidget(m_stack);
    m_maximizedPage->setObjectName(QStringLiteral("observatoryMaximizedPage"));
    auto* maximizedLayout = new QVBoxLayout(m_maximizedPage);
    maximizedLayout->setContentsMargins(0, 0, 0, 0);
    maximizedLayout->setSpacing(0);
    m_stack->addWidget(m_maximizedPage);
    m_stack->addWidget(m_floatingImages);
    m_stack->setCurrentWidget(m_dockArea);

    // The active view's history under the dock area, its toolbar floating over it.
    m_strip = new an::ui::AnHistoryStrip(this);
    m_strip->setObjectName(QStringLiteral("observatoryHistoryStrip"));
    layout->addWidget(m_strip);
    m_toolbar = new an::ui::AnFloatingToolbar(this);
    m_toolbar->setObjectName(QStringLiteral("observatoryFloatingToolbar"));
    m_toolbar->hide();
    m_binder = std::make_unique<an::ui::shell::ViewChromeBinder>(m_toolbar, m_strip);
    m_clearance = std::make_unique<ToolbarClearance>([this]() { placeToolbar(); });
}

WorkspacePage::~WorkspacePage()
{
    // The players stop telling the page before its views go, and the binder lets go of the view before the toolbar and
    // the strip go with the page.
    m_clearance.reset();
    m_binder.reset();
}

KDDockWidgets::QtWidgets::MainWindow* WorkspacePage::dockArea() const noexcept
{
    return m_dockArea;
}

FloatingImagesPage* WorkspacePage::floatingImages() const noexcept
{
    return m_floatingImages;
}

void WorkspacePage::maximize(ImageViewControl& view)
{
    if (m_maximized == &view)
    {
        return;
    }
    if (m_maximized != nullptr)
    {
        restore(*m_maximized);
    }
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    if (dock == nullptr)
    {
        return;
    }
    // The dock keeps its place in the area with a stand-in while its view is on the page.
    dock->setWidget(new QWidget());
    m_maximizedPage->layout()->addWidget(&view);
    view.show();
    m_maximized = &view;
    m_stack->setCurrentWidget(m_maximizedPage);
    placeToolbar();
}

void WorkspacePage::restore(ImageViewControl& view)
{
    if (m_maximized != &view)
    {
        return;
    }
    m_maximizedPage->layout()->removeWidget(&view);
    m_maximized = nullptr;
    if (KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget())
    {
        QWidget* const standIn = dock->widget();
        dock->setWidget(&view);
        if (standIn != nullptr && standIn != &view)
        {
            delete standIn;
        }
    }
    view.show();
    m_stack->setCurrentWidget(m_dockArea);
    placeToolbar();
}

ImageViewControl* WorkspacePage::maximizedView() const noexcept
{
    return m_maximized;
}

void WorkspacePage::showFloatingImages(const bool shown)
{
    if (m_maximized != nullptr)
    {
        return;
    }
    m_stack->setCurrentWidget(shown ? static_cast<QWidget*>(m_floatingImages) : static_cast<QWidget*>(m_dockArea));
    placeToolbar();
}

bool WorkspacePage::isFloatingImagesShown() const
{
    return m_stack->currentWidget() == m_floatingImages;
}

an::ui::AnFloatingToolbar* WorkspacePage::toolbar() const noexcept
{
    return m_toolbar;
}

an::ui::AnHistoryStrip* WorkspacePage::historyStrip() const noexcept
{
    return m_strip;
}

an::ui::shell::ViewChromeBinder& WorkspacePage::binder() const noexcept
{
    return *m_binder;
}

void WorkspacePage::bindView(ImageViewControl* view)
{
    m_binder->bind(view);
    placeToolbar();
}

QRect WorkspacePage::canvasArea() const
{
    return m_stack->geometry();
}

void WorkspacePage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeToolbar();
}

void WorkspacePage::placeToolbar()
{
    // The toolbar is shown over a view: not over the list of floating images, nor with no view to show.
    const bool shown = m_binder->toolbarView() != nullptr && !isFloatingImagesShown();
    m_toolbar->setVisible(shown);
    if (shown && m_clearance)
    {
        // Clear of the players shown under it.
        m_toolbar->placeOver(canvasArea(), m_clearance->playersIn(*m_stack->currentWidget(), *this));
        m_toolbar->raise();
    }
}

} // namespace an::ui::observatory
