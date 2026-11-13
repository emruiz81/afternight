// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The decorations, made through the application's view factory: a floating window that holds one task shows the
// task and nothing else of the dock library's - no tab strip, no second title bar - and the window that shows the
// drop indicators, with the band that marks where a dock widget would land, can be a class of the product's own.

#include "DockingTestSupport.h"

#include <QStackedWidget>

namespace an::tests::docking
{

namespace
{

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

} // namespace an::tests::docking
