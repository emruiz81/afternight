// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A floating window and the dock area under the Observatory profile. A floating image window is headed by the smaller
// strip, which becomes the density's strip when its view is docked again; the window is the first surface inside a
// one-pixel frame, the outline, or the accent while the window is active; its title bar, where the platform shows the
// library's, is the smaller strip with the title in the primary text. Docked groups lie two pixels apart on the dock
// area's ground, the accent's line marking a separator under the pointer, and each group is the first surface.

#include "ObservatoryDockingSupport.h"

#include <QApplication>

#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/TitleBar.h>

namespace an::tests::docking
{

class FloatingWindow : public ObservatoryDockingTest
{
};

class DockArea : public ObservatoryDockingTest
{
};

// The strip of a floating image window is the smaller one, in both densities; docked again, the view's strip is the
// density's.
TEST_F(FloatingWindow, ObservatoryImageWindowHasTheSmallerStrip)
{
    for (const ObservatoryLook& look : {observatoryLooks()[0], observatoryLooks()[3]})
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const auto workspaces = makeWorkspaceStack();
        auto* view = makeView(QStringLiteral("floating"));
        floatAt(view, QRect(520, 40, 320, 260));
        ASSERT_TRUE(view->isFloating());

        AN_TabBar* tabBar = tabBarOf(view);
        ASSERT_NE(tabBar, nullptr);
        const int smaller = an::ui::observatoryDockStripHeight(true);
        EXPECT_LT(smaller, t.metrics.strip);
        EXPECT_EQ(tabBar->height(), smaller);
        EXPECT_EQ(tabBar->tabRect(0).height(), smaller);
        QAbstractButton* floatButton = stripButton(view, KDDockWidgets::TitleBarButtonType::Float);
        ASSERT_NE(floatButton, nullptr);
        EXPECT_EQ(floatButton->toolTip(), QStringLiteral("Dock")) << "in a floating window the button docks the view";
        EXPECT_LE(placeIn(floatButton, stackOf(view)).y() + floatButton->height(), smaller - 1);

        workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
        settle();
        ASSERT_FALSE(view->isFloating());
        tabBar = tabBarOf(view);
        ASSERT_NE(tabBar, nullptr);
        EXPECT_EQ(tabBar->height(), t.metrics.strip) << "docked, the strip is the density's";
        floatButton = stripButton(view, KDDockWidgets::TitleBarButtonType::Float);
        ASSERT_NE(floatButton, nullptr);
        EXPECT_EQ(floatButton->toolTip(), QStringLiteral("Float / Detach"));
    }
}

// The window is the first surface inside a one-pixel frame: the outline, and the accent while the window is active.
TEST_F(FloatingWindow, ObservatoryFrameIsTheOutlineAndTheAccentWhileActive)
{
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        auto* view = makeView(QStringLiteral("floating"));
        floatAt(view, QRect(520, 40, 320, 260));
        QWidget* window = windowOf(view);
        ASSERT_NE(dynamic_cast<AN_FloatingWindow*>(window), nullptr);

        auto* other = own(new QWidget);
        other->setGeometry(40, 400, 120, 80);
        other->show();
        other->activateWindow();
        settle();
        ASSERT_FALSE(window->isActiveWindow());
        RenderedPicture inactive(window->size(), 1.0, t.layers.surface0, [window](QPainter& painter) {
            window->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        });
        const QColor outline = an::ui::compositeOver(t.colors.borderPrimary, t.layers.surface1);
        expectColour(inactive.at(0.5, window->height() / 2.0), outline, "the frame");
        expectColour(inactive.at(window->width() - 0.5, window->height() - 0.5), outline, "the frame's corner");
        expectColour(inactive.at(2.5, window->height() - 2.5), t.layers.surface1, "the window's ground");

        window->activateWindow();
        settle();
        ASSERT_TRUE(window->isActiveWindow());
        RenderedPicture active(window->size(), 1.0, t.layers.surface0, [window](QPainter& painter) {
            window->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        });
        expectColour(active.at(0.5, window->height() / 2.0), t.colors.accentPrimary, "the active window's frame");
    }
}

// The library's title bar of a floating window - shown where the platform's is not - is the smaller strip: the first
// surface, a hairline under it, the title in the primary text.
TEST_F(FloatingWindow, ObservatoryTitleBarIsTheSmallerStrip)
{
    for (const ObservatoryLook& look : {observatoryLooks()[0], observatoryLooks()[1], observatoryLooks()[5]})
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        auto* view = makeView(QStringLiteral("floating"));
        view->setTitle(QStringLiteral("M 42"));
        floatAt(view, QRect(520, 40, 320, 260));
        auto* floatingWindow = view->dockWidget()->floatingWindow();
        ASSERT_NE(floatingWindow, nullptr);
        auto* titleBar = dynamic_cast<AN_TitleBar*>(widgetOf(floatingWindow->titleBar()->view()));
        ASSERT_NE(titleBar, nullptr);

        const int smaller = an::ui::observatoryDockStripHeight(true);
        EXPECT_EQ(titleBar->sizeHint().height(), smaller);
        titleBar->resize(300, smaller);
        const RenderedPicture picture = RenderedPicture::ofWidget(*titleBar, 1.0, t.layers.surface0);
        expectColour(picture.at(150.5, 2.5), t.layers.surface1, "the title bar's ground");
        expectColour(picture.at(150.5, smaller - 0.5), an::ui::compositeOver(t.colors.borderSubtle, t.layers.surface1),
                     "the hairline");

        PaintRecorder recorder(titleBar->size());
        recordPainting(titleBar, recorder);
        const auto* title = recorder.textHolding(QStringLiteral("M 42"));
        ASSERT_NE(title, nullptr);
        expectColour(title->colour, t.colors.textPrimary, "the title", 0);
    }
}

// Groups lie two pixels apart: the separator the profile gives the library is the dock area's ground, and the accent's
// line under the pointer; a group's ground is the first surface.
TEST_F(DockArea, ObservatorySeparatorsAreTwoPixelsOfTheGround)
{
    EXPECT_EQ(KDDockWidgets::Config::self().separatorThickness(), an::ui::kObservatoryDockSeparator);
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const auto workspaces = makeWorkspaceStack();
        auto* left = makeView(QStringLiteral("left"));
        auto* right = makeView(QStringLiteral("right"));
        workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
        workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
        settle();

        const auto separators = childrenOfType<AN_Separator>(workspaces.first);
        ASSERT_EQ(separators.size(), 1);
        AN_Separator* separator = separators.constFirst();
        EXPECT_EQ(separator->width(), an::ui::kObservatoryDockSeparator);
        auto* leftGroup = widgetOf(left->group()->view());
        auto* rightGroup = widgetOf(right->group()->view());
        ASSERT_NE(leftGroup, rightGroup);
        const int gap =
            placeIn(rightGroup, workspaces.first).x() - (placeIn(leftGroup, workspaces.first).x() + leftGroup->width());
        EXPECT_EQ(gap, an::ui::kObservatoryDockSeparator) << "the groups lie the separator apart";

        const RenderedPicture rest = RenderedPicture::ofWidget(*separator, 1.0, t.colors.textPrimary);
        expectColour(rest.at(1.0, separator->height() / 2.0), t.layers.surface0, "the separator");
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(separator, &enter);
        const RenderedPicture hovered = RenderedPicture::ofWidget(*separator, 1.0, t.colors.textPrimary);
        expectColour(hovered.at(1.0, separator->height() / 2.0),
                     an::ui::compositeOver(t.colors.accentLine, t.layers.surface0), "the separator under the pointer");
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(separator, &leave);

        const auto groups = childrenOfType<AN_Group>(workspaces.first);
        ASSERT_FALSE(groups.isEmpty());
        AN_Group* group = groups.constFirst();
        RenderedPicture pane(group->size(), 1.0, t.colors.textPrimary, [group](QPainter& painter) {
            group->QWidget::render(&painter, QPoint(), QRegion(), QWidget::RenderFlags());
        });
        expectColour(pane.at(group->width() / 2.0, group->height() - 1.5), t.layers.surface1, "the group's ground");
    }
}

} // namespace an::tests::docking
