// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The decorations, made through the application's view factory: a floating window that holds one task shows the
// task and nothing else of the dock library's - no tab strip, no second title bar - and the window that shows the
// drop indicators, with the band that marks where a dock widget would land, can be a class of the product's own.

#include "DockingTestSupport.h"

#include "controls/DockDropIndicators.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "theme/KddwProxyStyle.h"
#include "theme/ObservatoryDock.h"

#include <QAbstractButton>
#include <QStackedWidget>
#include <QTabBar>

#include <cstdint>

namespace an::tests::docking
{

namespace
{

/// The dock library's own separator: what it lays groups apart by unless told otherwise.
constexpr int kLibrarySeparator = 5;

template <typename T> int visibleCount(const QWidget* window)
{
    int count = 0;
    const auto found = window->findChildren<T*>();
    for (const auto* widget : found)
    {
        if (widget->isVisible())
        {
            ++count;
        }
    }
    return count;
}

} // namespace

class Decorations : public DockingTest
{
};

TEST_F(Decorations, AFloatingViewShowsItsTabStrip)
{
    auto* view = makeView(QStringLiteral("view"));
    floatAt(view, QRect(520, 40, 300, 240));

    auto* window = windowOf(view);
    EXPECT_NE(dynamic_cast<AN_FloatingWindow*>(window), nullptr) << "the window is the application's decoration";
    EXPECT_EQ(visibleCount<AN_TabBar>(window), 1);
    EXPECT_EQ(visibleCount<AN_TitleBar>(window), 0) << "a floating image window is headed by its tab strip";
    EXPECT_GT(placeIn(view, window).y(), 12) << "the strip stands above the image";
}

TEST_F(Decorations, ATaskWindowShowsNoTabStripAndNoSecondTitleBar)
{
    auto* task = makeTask(QStringLiteral("task"));
    showAsTaskWindow(task, QRect(520, 40, 300, 240));

    auto* window = windowOf(task);
    ASSERT_TRUE(task->isFloating());
    EXPECT_NE(dynamic_cast<AN_FloatingWindow*>(window), nullptr) << "the window is the application's decoration";
    EXPECT_EQ(visibleCount<AN_TabBar>(window), 0);

    if (floatingWindowsUseTheNativeTitleBar())
    {
        EXPECT_EQ(visibleCount<AN_TitleBar>(window), 0) << "the system's title bar is the only one";
        EXPECT_LE(placeIn(task, window).y(), 8) << "the task starts at the top of the window";
    }
    else
    {
        EXPECT_EQ(visibleCount<AN_TitleBar>(window), 1) << "the application's title bar is the only one";
        const auto titleBars = window->findChildren<AN_TitleBar*>();
        int below = 0;
        for (const auto* titleBar : titleBars)
        {
            if (titleBar->isVisible())
            {
                below = placeIn(titleBar, window).y() + titleBar->height();
            }
        }
        EXPECT_LE(placeIn(task, window).y(), below + 8) << "the task starts under the title bar";
    }

    EXPECT_GE(task->height(), window->height() - 48) << "the task has the window's height, less one title bar at most";
}

TEST_F(Decorations, ATaskWindowKeepsItsDecorationsBesideAViewsWindow)
{
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    floatAt(view, QRect(40, 40, 300, 240));
    showAsTaskWindow(task, QRect(420, 40, 300, 240));

    EXPECT_EQ(visibleCount<AN_TabBar>(windowOf(view)), 1) << "what a task's window hides, a view's window still shows";
    EXPECT_EQ(visibleCount<AN_TabBar>(windowOf(task)), 0);

    // Dragged and released, the task's window is the same window.
    auto drag = SimulatedDrag::ofFloatingWindow(task);
    drag.moveTo(QPoint(500, 500));
    drag.release();

    ASSERT_TRUE(task->isFloating());
    EXPECT_EQ(visibleCount<AN_TabBar>(windowOf(task)), 0);
}

TEST_F(Decorations, TheIndicatorWindowAndTheBandAreTheOnesTheFactoryMakes)
{
    ProbeViewFactory::setReplaceIndicators(true);
    const auto workspaces = makeWorkspaceStack();
    auto* docked = makeView(QStringLiteral("docked"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(docked, KDDockWidgets::Location_OnLeft);
    floatAt(floating, QRect(520, 40, 240, 200));

    auto* indicators = dynamic_cast<KDDockWidgets::Core::ClassicDropIndicatorOverlay*>(indicatorsOf(workspaces.first));
    ASSERT_NE(indicators, nullptr);
    auto* indicatorWindow = dynamic_cast<ProbeIndicatorWindow*>(indicators->indicatorWindow());
    ASSERT_NE(indicatorWindow, nullptr) << "the dock area's indicator window is the one the factory made";
    auto* band = widgetOf(indicators->rubberBand());
    ASSERT_NE(band, nullptr);
    EXPECT_TRUE(isProbeRubberBand(band));
    EXPECT_FALSE(indicatorWindow->isVisible());
    EXPECT_FALSE(band->isVisible());

    auto drag = SimulatedDrag::ofFloatingWindow(floating);
    const QPoint overDocked = docked->mapToGlobal(docked->rect().center());
    ASSERT_TRUE(drag.moveOnto(indicators, overDocked, KDDockWidgets::DropLocation_Left));

    EXPECT_TRUE(indicatorWindow->isVisible());
    EXPECT_GT(indicatorWindow->hoverCount(), 0) << "the library asks the factory's window what is under the cursor";
    EXPECT_TRUE(band->isVisible());
    const QRect bandInWorkspace(placeIn(band, workspaces.first), band->size());
    EXPECT_LE(bandInWorkspace.right(), workspaces.first->width() / 2 + 8)
        << "the band marks the half where the dock widget would land";
    EXPECT_GE(bandInWorkspace.height(), workspaces.first->height() / 2);

    drag.release();

    EXPECT_FALSE(indicatorWindow->isVisible());
    EXPECT_FALSE(band->isVisible());
    EXPECT_FALSE(floating->isFloating());
    EXPECT_LT(floating->mapToGlobal(QPoint(0, 0)).x(), docked->mapToGlobal(QPoint(0, 0)).x());
}

// Under the Classic profile every decoration paints through the code it had before the Observatory look existed: no
// Observatory painter of the decorations or of the application's style runs while a dock area with tabs, a separator,
// a floating window, the indicators and the band is painted, the library's own tab strip style and indicator window are
// kept, and the separator is the library's.
TEST_F(Decorations, ClassicUnchanged)
{
    ASSERT_EQ(an::ui::styleProfile(), an::ui::StyleProfile::Classic);
    an::ui::applyDockDecorationsProfile();
    EXPECT_EQ(KDDockWidgets::Config::self().separatorThickness(), kLibrarySeparator);

    const std::uint64_t dockPaints = an::ui::testing::observatoryDockPaints();
    const std::uint64_t stylePaints = an::ui::testing::observatoryPaints();

    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* tabbed = makeView(QStringLiteral("tabbed"));
    auto* right = makeView(QStringLiteral("right"));
    auto* floating = makeView(QStringLiteral("floating"));
    left->setTitle(QStringLiteral("M 42 *"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    left->addDockWidgetAsTab(tabbed);
    workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
    floatAt(floating, QRect(520, 40, 240, 200));
    an::ui::setTabAdornment(tabbed, {QStringLiteral("eye"), QStringLiteral("a task"), true, {}});
    settle();

    for (auto* tabBar : workspaces.first->findChildren<AN_TabBar*>())
    {
        EXPECT_EQ(dynamic_cast<an::ui::KddwProxyStyle*>(tabBar->style()), nullptr) << "the library's tab strip style";
        for (int index = 0; index < tabBar->count(); ++index)
        {
            EXPECT_TRUE(tabBar->adornmentRect(index).isEmpty()) << "an adornment is the Observatory look's";
            if (const QWidget* close = tabBar->tabButton(index, QTabBar::RightSide))
            {
                EXPECT_TRUE(close->isEnabled()) << "a lock is the Observatory look's";
            }
        }
    }
    for (auto* stack : workspaces.first->findChildren<AN_Stack*>())
    {
        for (const auto type : {KDDockWidgets::TitleBarButtonType::Float, KDDockWidgets::TitleBarButtonType::Close})
        {
            if (const QAbstractButton* button = stack->button(type))
            {
                EXPECT_TRUE(button->isEnabled()) << "a lock is the Observatory look's";
            }
        }
        EXPECT_EQ(stack->findChild<QAbstractButton*>(QStringLiteral("anDockTabList")), nullptr);
    }
    EXPECT_FALSE(workspaces.first->findChildren<KDDockWidgets::QtWidgets::Separator*>().isEmpty());

    // Everything painted once: the workspace, the floating window, and the indicators and band of a drag over the
    // workspace.
    (void)workspaces.window->grab();
    (void)windowOf(floating)->grab();
    auto* indicators = dynamic_cast<KDDockWidgets::Core::ClassicDropIndicatorOverlay*>(indicatorsOf(workspaces.first));
    ASSERT_NE(indicators, nullptr);
    EXPECT_EQ(dynamic_cast<AN_DropIndicators*>(indicators->indicatorWindow()), nullptr);
    EXPECT_EQ(dynamic_cast<AN_DropBand*>(widgetOf(indicators->rubberBand())), nullptr);
    auto drag = SimulatedDrag::ofFloatingWindow(floating);
    ASSERT_TRUE(
        drag.moveOnto(indicators, right->mapToGlobal(right->rect().center()), KDDockWidgets::DropLocation_Left));
    (void)widgetOf(indicators->rubberBand())->grab();
    (void)workspaces.window->grab();
    drag.release();
    (void)workspaces.window->grab();

    EXPECT_EQ(an::ui::testing::observatoryDockPaints(), dockPaints) << "no Observatory painter of the decorations ran";
    EXPECT_EQ(an::ui::testing::observatoryPaints(), stylePaints) << "no Observatory painter of the style ran";
}

} // namespace an::tests::docking
