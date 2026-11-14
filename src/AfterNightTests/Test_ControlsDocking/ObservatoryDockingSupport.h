// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// What the cases of the dock decorations under the Observatory profile share: the profile, a look and the separator
// the profile gives the dock library, in force while a case's windows live; the strips and buttons of a dock widget's
// group; and how a picture of them is read.

#include "DockingTestSupport.h"

#include "fixtures/ObservatoryLook.h"
#include "fixtures/PaintRecorder.h"

#include "controls/KDDockWidgetsHelper.h"
#include "theme/AnDesignTokens.h"
#include "theme/KddwProxyStyle.h"
#include "theme/ObservatoryDock.h"

#include <QAbstractButton>
#include <QTabBar>

#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Stack.h>
#include <kddockwidgets/qtwidgets/views/Stack.h>

#include <optional>
#include <string>

namespace an::tests::docking
{

using an::ui::testing::colourName;
using an::ui::testing::layered;
using an::ui::testing::ObservatoryLook;
using an::ui::testing::observatoryLooks;
using an::ui::testing::ObservatoryStyleScope;
using an::ui::testing::PaintRecorder;
using an::ui::testing::RenderedPicture;
using an::ui::testing::sameColour;

/// A case of the dock decorations under the Observatory profile, in the first look unless it puts another in force.
/// The profile, the look and the separator are put in force before the case makes a window, while the dock library
/// still takes a separator, and put back once its windows are gone.
class ObservatoryDockingTest : public DockingTest
{
protected:
    void SetUp() override
    {
        DockingTest::SetUp();
        m_scope.emplace(observatoryLooks()[0]);
        an::ui::applyDockDecorationsProfile();
    }

    void TearDown() override
    {
        DockingTest::TearDown();
        m_scope.reset();
        // The application's style, as the suite installed it for the cases that follow, and the Classic separator.
        an::ui::installAnProxyStyle();
        an::ui::applyDockDecorationsProfile();
    }

    /// Puts a look in force. Windows the case makes afterwards are made in it.
    void setLook(const ObservatoryLook& look)
    {
        m_scope->setLook(look);
    }

private:
    std::optional<ObservatoryStyleScope> m_scope;
};

/// The stack, the tab strip and a strip button of the group a dock widget is in.
inline AN_Stack* stackOf(const DockWidget* dockWidget)
{
    auto* group = dockWidget->group();
    return group != nullptr ? dynamic_cast<AN_Stack*>(widgetOf(group->stack()->view())) : nullptr;
}

inline AN_TabBar* tabBarOf(const DockWidget* dockWidget)
{
    AN_Stack* stack = stackOf(dockWidget);
    return stack != nullptr ? dynamic_cast<AN_TabBar*>(stack->tabBar()) : nullptr;
}

inline QAbstractButton* stripButton(const DockWidget* dockWidget, const KDDockWidgets::TitleBarButtonType type)
{
    AN_Stack* stack = stackOf(dockWidget);
    return stack != nullptr ? stack->button(type) : nullptr;
}

/// A tab's rectangle in its stack's coordinates.
inline QRect tabInStack(const AN_TabBar* tabBar, const int index)
{
    return tabBar->tabRect(index).translated(placeIn(tabBar, tabBar->parentWidget()));
}

/// Expects a colour, saying where it was read and what was there.
inline void expectColour(const QColor& actual, const QColor& expected, const std::string& where,
                         const int tolerance = 2)
{
    EXPECT_TRUE(sameColour(actual, expected, tolerance))
        << where << ": " << colourName(actual) << ", expected " << colourName(expected);
}

/// The widgets of a class inside a widget, for the dock classes that carry no meta-object of their own.
template <typename T> QList<T*> childrenOfType(const QWidget* widget)
{
    QList<T*> found;
    for (QWidget* child : widget->findChildren<QWidget*>())
    {
        if (auto* typed = dynamic_cast<T*>(child))
        {
            found.append(typed);
        }
    }
    return found;
}

/// Renders a widget onto a recorder of what it paints as text. The dock library's views give `render` another meaning.
inline void recordPainting(QWidget* widget, PaintRecorder& recorder)
{
    widget->QWidget::render(&recorder);
}

/// Delivers a click - a press and a release - of the left button at a place of a widget.
inline void clickAt(QWidget* widget, const QPoint local)
{
    const QPoint global = widget->mapToGlobal(local);
    deliverMouse(widget, QEvent::MouseButtonPress, global);
    deliverMouse(widget, QEvent::MouseButtonRelease, global);
    settle();
}

} // namespace an::tests::docking
