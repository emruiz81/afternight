// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/WorkspacePage.h"

#include "controls/ImageViewControl.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QStackedWidget>
#include <QVBoxLayout>

namespace an::ui::observatory
{

WorkspacePage::WorkspacePage(KDDockWidgets::QtWidgets::MainWindow& dockArea, QWidget* parent)
    : QWidget(parent), m_dockArea(&dockArea)
{
    setObjectName(QStringLiteral("observatoryWorkspacePage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_stack->addWidget(m_dockArea);
    m_maximizedPage = new QWidget(m_stack);
    auto* maximizedLayout = new QVBoxLayout(m_maximizedPage);
    maximizedLayout->setContentsMargins(0, 0, 0, 0);
    maximizedLayout->setSpacing(0);
    m_stack->addWidget(m_maximizedPage);
    m_stack->setCurrentWidget(m_dockArea);
}

KDDockWidgets::QtWidgets::MainWindow* WorkspacePage::dockArea() const noexcept
{
    return m_dockArea;
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
}

ImageViewControl* WorkspacePage::maximizedView() const noexcept
{
    return m_maximized;
}

} // namespace an::ui::observatory
