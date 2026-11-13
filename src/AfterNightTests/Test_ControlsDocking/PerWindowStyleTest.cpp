// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The dock style on several main windows: each workspace has its own, each gives its workspace the dock palette, and
// both workspaces are drawn alike.

#include "DockingTestSupport.h"

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QStackedWidget>

#include "theme/AnDesignTokens.h"
#include "theme/KddwProxyStyle.h"

namespace an::tests::docking
{

class PerWindowStyle : public DockingTest
{
};

TEST_F(PerWindowStyle, EachMainWindowKeepsTheStyleInstalledOnIt)
{
    const auto workspaces = makeWorkspaceStack();

    auto* firstStyle = an::ui::installKddwProxyStyle(workspaces.first);
    auto* secondStyle = an::ui::installKddwProxyStyle(workspaces.second);

    ASSERT_NE(firstStyle, nullptr);
    ASSERT_NE(secondStyle, nullptr);
    EXPECT_NE(firstStyle, secondStyle);
    EXPECT_EQ(workspaces.first->style(), firstStyle);
    EXPECT_EQ(workspaces.second->style(), secondStyle);
    EXPECT_EQ(firstStyle->parent(), workspaces.first) << "a workspace's style goes with the workspace";
    EXPECT_EQ(secondStyle->parent(), workspaces.second);
}

TEST_F(PerWindowStyle, TheStyleOfAMainWindowIsHandedToNoWidgetInsideIt)
{
    const auto workspaces = makeWorkspaceStack();
    auto* style = an::ui::installKddwProxyStyle(workspaces.first);
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
    settle();

    // A style set on a widget is that widget's alone. What the dock style gives the tab strips, groups, separators
    // and buttons inside a workspace is the palette it sets on the workspace, which they inherit; they are drawn by
    // their own paint events and by the application's style.
    const auto children = workspaces.first->findChildren<QWidget*>();
    ASSERT_GT(children.size(), 8);
    for (const auto* child : children)
    {
        EXPECT_NE(child->style(), style) << child->metaObject()->className();
    }
    ASSERT_FALSE(workspaces.first->findChildren<AN_Stack*>().isEmpty());
    EXPECT_EQ(workspaces.first->findChildren<AN_Stack*>().constFirst()->style(), QApplication::style());
}

TEST_F(PerWindowStyle, RefreshingEachStyleGivesEachMainWindowTheDockPalette)
{
    const auto workspaces = makeWorkspaceStack();
    auto* first = makeView(QStringLiteral("first"));
    auto* second = makeView(QStringLiteral("second"));
    workspaces.first->addDockWidget(first, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    auto* firstStyle = an::ui::installKddwProxyStyle(workspaces.first);
    auto* secondStyle = an::ui::installKddwProxyStyle(workspaces.second);

    // What a change of theme asks of the dock style. The query for "the installed style" names one of them; a shell
    // with several workspaces refreshes each.
    firstStyle->refreshThemeTokens();
    secondStyle->refreshThemeTokens();

    const auto& tokens = an::ui::tokens();
    for (auto* workspace : {workspaces.first, workspaces.second})
    {
        EXPECT_EQ(workspace->palette().color(QPalette::Window), tokens.materials.micaSurface);
        EXPECT_EQ(workspace->palette().color(QPalette::Mid), tokens.colors.borderSubtle);
    }

    // The dock children take the palette from the main window they are in.
    for (auto* dockWidget : {first, second})
    {
        const auto tabBars = mainWindowHolding(dockWidget)->findChildren<AN_TabBar*>();
        ASSERT_FALSE(tabBars.isEmpty());
        EXPECT_EQ(tabBars.constFirst()->palette().color(QPalette::Mid), tokens.colors.borderSubtle);
    }
}

TEST_F(PerWindowStyle, BothMainWindowsAreDrawnAlike)
{
    const auto workspaces = makeWorkspaceStack();
    // The same title in both, so the two pictures can be compared whole.
    auto* first = makeView(QStringLiteral("first"));
    auto* second = makeView(QStringLiteral("second"));
    first->setTitle(QStringLiteral("M 42"));
    second->setTitle(QStringLiteral("M 42"));
    workspaces.first->addDockWidget(first, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    an::ui::installKddwProxyStyle(workspaces.first)->refreshThemeTokens();
    an::ui::installKddwProxyStyle(workspaces.second)->refreshThemeTokens();

    workspaces.show(workspaces.first);
    const QImage firstPicture = workspaces.first->grab().toImage();
    workspaces.show(workspaces.second);
    const QImage secondPicture = workspaces.second->grab().toImage();

    ASSERT_FALSE(firstPicture.isNull());
    ASSERT_EQ(firstPicture.size(), secondPicture.size());
    EXPECT_EQ(firstPicture, secondPicture) << "the second workspace is drawn as the first is";

    // And the picture is the dock decorations', not an empty page: the strip's tab is drawn over the dock area.
    const auto tabBars = workspaces.second->findChildren<AN_TabBar*>();
    ASSERT_EQ(tabBars.size(), 1);
    const QRect strip(placeIn(tabBars.constFirst(), workspaces.second), tabBars.constFirst()->size());
    ASSERT_GT(strip.height(), 8);
    const QRgb inTheStrip = secondPicture.pixel(strip.left() + 3, strip.center().y());
    const QRgb inTheView =
        secondPicture.pixel(secondPicture.width() / 2, strip.bottom() + (secondPicture.height() - strip.bottom()) / 2);
    EXPECT_NE(inTheStrip, inTheView);
}

} // namespace an::tests::docking
