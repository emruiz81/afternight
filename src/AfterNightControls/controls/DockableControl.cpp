// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DockableControl.h"

#include "KDDockWidgetsHelper.h"

#include <verdigris/wobjectimpl.h>

DockableControl::DockableControl(DockableControlType type, QWidget* parent, Qt::WindowFlags f)
    : QWidget(parent, f), m_DockableControlType(type)
{
}

AN_TabBar* DockableControl::getTabBar()
{
    if (auto* s = getStack())
        return s->findChild<AN_TabBar*>("DockTabBar");
    return nullptr;
}

AN_Stack* DockableControl::getStack()
{
    auto* dw = getDockWidget();
    if (!dw)
        return nullptr;

    for (QWidget* p = dw->parentWidget(); p; p = p->parentWidget())
        if (auto* s = qobject_cast<AN_Stack*>(p))
            return s;
    return nullptr;
}

DockableControl::DockableControlType DockableControl::getType() const
{
    return m_DockableControlType;
}

DockableControl::~DockableControl() {}

void DockableControl::updateTabBarHighlight(bool focused)
{
    if (auto* tb = getTabBar())
        tb->setForceUnselected(!focused); // focused/current => show selected; otherwise dim
}

W_OBJECT_IMPL(CustomDockWidget)

CustomDockWidget::CustomDockWidget(const QString& uniqueName, KDDockWidgets::DockWidgetOptions options,
                                   KDDockWidgets::LayoutSaverOptions layoutSaverOptions, Qt::WindowFlags windowFlags)
    : KDDockWidgets::QtWidgets::DockWidget(uniqueName, options, layoutSaverOptions, windowFlags)
{
}

void CustomDockWidget::closeEvent(QCloseEvent* ev)
{
    ev->ignore();
    emit closeRequested();
}
