// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A group's tab strip under the Observatory profile: a strip of the density's height on the first surface with a
// hairline under it; a tab's name in the small text, elided in the middle and never cut under twelve characters, the
// whole name its tooltip; a star in the accent's text colour for unsaved changes; the current tab over the accent's
// bar, ten pixels in from its ends, or the tertiary text's bar in a group that is not the active view's; an adornment
// after the name that is drawn and answers a click; a locked tab's buttons dimmed; and the list of the strip's tabs.

#include "ObservatoryDockingSupport.h"

#include <QMenu>

#include <algorithm>
#include <functional>

namespace an::tests::docking
{

namespace
{

using KDDockWidgets::TitleBarButtonType;

/// A workspace showing two views as tabs of one group, the first current.
struct TwoTabs
{
    WorkspaceStack workspaces;
    DockWidget* first = nullptr;
    DockWidget* second = nullptr;
};

} // namespace

class TabStrip : public ObservatoryDockingTest
{
protected:
    TwoTabs makeTwoTabs(const QString& firstTitle = QStringLiteral("M 42"),
                        const QString& secondTitle = QStringLiteral("NGC 7000"))
    {
        TwoTabs tabs;
        tabs.workspaces = makeWorkspaceStack();
        tabs.first = makeView(QStringLiteral("first"));
        tabs.second = makeView(QStringLiteral("second"));
        tabs.first->setTitle(firstTitle);
        tabs.second->setTitle(secondTitle);
        tabs.workspaces.first->addDockWidget(tabs.first, KDDockWidgets::Location_OnLeft);
        tabs.first->addDockWidgetAsTab(tabs.second);
        tabs.first->setAsCurrentTab();
        settle();
        return tabs;
    }
};

// The strip is the density's strip high, in every look; its buttons are small controls; a change of density reaches a
// strip that exists when the theme's refresh tells it.
TEST_F(TabStrip, ObservatoryStripHeightFollowsTheDensity)
{
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const TwoTabs tabs = makeTwoTabs();
        AN_TabBar* tabBar = tabBarOf(tabs.first);
        ASSERT_NE(tabBar, nullptr);
        EXPECT_NE(dynamic_cast<an::ui::KddwProxyStyle*>(tabBar->style()), nullptr)
            << "the dock style draws the library's tab strip";
        EXPECT_EQ(tabBar->height(), t.metrics.strip);
        for (const int index : {0, 1})
        {
            EXPECT_EQ(tabBar->tabRect(index).height(), t.metrics.strip);
        }
        for (const auto type : {TitleBarButtonType::Float, TitleBarButtonType::Close})
        {
            QAbstractButton* button = stripButton(tabs.first, type);
            ASSERT_NE(button, nullptr);
            EXPECT_EQ(button->size(), QSize(t.metrics.controlSmall, t.metrics.controlSmall));
            const QRect inStack(placeIn(button, stackOf(tabs.first)), button->size());
            EXPECT_GE(inStack.top(), 0);
            EXPECT_LE(inStack.bottom(), t.metrics.strip - 2) << "the buttons stand above the hairline";
        }
        // The close button of each tab lies where the tab's parts put it.
        for (const int index : {0, 1})
        {
            const QWidget* close = tabBar->tabButton(index, QTabBar::RightSide);
            ASSERT_NE(close, nullptr);
            EXPECT_EQ(close->geometry(),
                      an::ui::observatoryDockTabParts(tabBar->tabRect(index), tabBar->observatoryDockTab(index)).close);
        }
    }

    // A density changed while the strip lives: the refresh the theme runs lays it out again.
    setLook(observatoryLooks()[0]);
    const TwoTabs tabs = makeTwoTabs();
    AN_TabBar* tabBar = tabBarOf(tabs.first);
    ASSERT_NE(tabBar, nullptr);
    const int compact = an::ui::tokens().metrics.strip;
    EXPECT_EQ(tabBar->height(), compact);
    setLook(observatoryLooks()[3]);
    tabBar->applyThemeColors();
    stackOf(tabs.first)->applyThemeColors();
    settle();
    EXPECT_GT(an::ui::tokens().metrics.strip, compact);
    EXPECT_EQ(tabBar->height(), an::ui::tokens().metrics.strip);
}

// The strip's ground is the first surface with a hairline under it; the current tab stands over a three-pixel accent
// bar ten pixels in from its ends; the other tab has none; in a group that is not the active view's the bar is the
// tertiary text's.
TEST_F(TabStrip, ObservatoryActiveTabStandsOverTheBarInsetByTen)
{
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const TwoTabs tabs = makeTwoTabs();
        AN_Stack* stack = stackOf(tabs.first);
        AN_TabBar* tabBar = tabBarOf(tabs.first);
        ASSERT_NE(stack, nullptr);
        ASSERT_NE(tabBar, nullptr);
        ASSERT_EQ(tabBar->currentIndex(), 0);
        const QRect current = tabInStack(tabBar, 0);
        const QRect other = tabInStack(tabBar, 1);
        const int strip = t.metrics.strip;
        const qreal barMiddle = strip - 1 - 1.5;

        for (const qreal ratio : an::ui::testing::kRenderRatios)
        {
            SCOPED_TRACE(ratio);
            const RenderedPicture picture = RenderedPicture::ofWidget(*stack, ratio, t.layers.surface0);
            expectColour(picture.at(current.left() + 12, barMiddle), t.colors.accentPrimary, "the bar");
            expectColour(picture.at(current.right() - 12, barMiddle), t.colors.accentPrimary, "the bar's end");
            expectColour(picture.at(current.left() + 5, barMiddle), t.layers.surface1, "ten pixels in");
            expectColour(picture.at(current.right() - 5, barMiddle), t.layers.surface1, "ten pixels in, at the end");
            expectColour(picture.at(other.center().x(), barMiddle), t.layers.surface1, "no bar under the other tab");
            expectColour(picture.at(current.center().x(), strip - 0.5),
                         an::ui::compositeOver(t.colors.borderSubtle, t.layers.surface1), "the hairline");
            expectColour(picture.at(stack->width() - 2.5, 2.5), t.layers.surface1, "the strip's ground at its end");
        }

        // Not the active view's group: the bar is the tertiary text's, the current tab's text the secondary.
        tabBar->setForceUnselected(true);
        const RenderedPicture picture = RenderedPicture::ofWidget(*stack, 1.0, t.layers.surface0);
        expectColour(picture.at(current.left() + 12, barMiddle), t.colors.textTertiary, "the bar of another group");
        PaintRecorder recorder(tabBar->size());
        recordPainting(tabBar, recorder);
        const auto* name = recorder.textHolding(QStringLiteral("M 42"));
        ASSERT_NE(name, nullptr);
        expectColour(name->colour, t.colors.textSecondary, "the name in another group", 0);
        tabBar->setForceUnselected(false);
    }
}

// The name is the small text, semibold: the current tab's the primary text, the other's the secondary; an image with
// unsaved changes shows its name and then a star in the accent's text colour.
TEST_F(TabStrip, ObservatoryStarIsTheAccentText)
{
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const TwoTabs tabs = makeTwoTabs(QStringLiteral("M 42 *"), QStringLiteral("NGC 7000"));
        AN_TabBar* tabBar = tabBarOf(tabs.first);
        ASSERT_NE(tabBar, nullptr);

        const an::ui::ObservatoryDockTab tab = tabBar->observatoryDockTab(0);
        EXPECT_EQ(tab.name, QStringLiteral("M 42"));
        EXPECT_TRUE(tab.modified);
        EXPECT_FALSE(tabBar->observatoryDockTab(1).modified);

        PaintRecorder recorder(tabBar->size());
        recordPainting(tabBar, recorder);
        const auto* name = recorder.textHolding(QStringLiteral("M 42"));
        const auto* star = recorder.textHolding(QStringLiteral("*"));
        const auto* other = recorder.textHolding(QStringLiteral("NGC 7000"));
        ASSERT_NE(name, nullptr);
        ASSERT_NE(star, nullptr);
        ASSERT_NE(other, nullptr);
        EXPECT_FALSE(name->text.contains(QLatin1Char('*'))) << "the star is drawn apart from the name";
        expectColour(name->colour, t.colors.textPrimary, "the current tab's name", 0);
        expectColour(star->colour, t.colors.textAccent, "the star", 0);
        expectColour(other->colour, t.colors.textSecondary, "the other tab's name", 0);
        EXPECT_EQ(name->font.weight(), QFont::DemiBold);
        EXPECT_NEAR(name->font.pointSizeF() * 96.0 / 72.0, t.metrics.fontSmall, 0.01);
    }
}

// A name too long for its tab is elided in the middle - both of its ends stay - and a tab is never cut under twelve
// characters; the tab's tooltip is the whole name.
TEST_F(TabStrip, ElidesInTheMiddleAndKeepsTheFullNameAsTooltip)
{
    const QString longName = QStringLiteral("NGC7000_North_America_Nebula_HaOIIISII_final_v3");
    auto workspaces = makeWorkspaceStack();
    workspaces.window->resize(360, 300);
    settle();
    auto* first = makeView(QStringLiteral("first"));
    first->setTitle(longName);
    workspaces.first->addDockWidget(first, KDDockWidgets::Location_OnLeft);
    for (const QString& name : {QStringLiteral("second"), QStringLiteral("third")})
    {
        auto* view = makeView(name);
        view->setTitle(longName + QStringLiteral("_") + name);
        first->addDockWidgetAsTab(view);
    }
    first->setAsCurrentTab();
    settle();

    AN_TabBar* tabBar = tabBarOf(first);
    ASSERT_NE(tabBar, nullptr);
    ASSERT_EQ(tabBar->count(), 3);
    const int index = tabBar->currentIndex();
    const an::ui::ObservatoryDockTab tab = tabBar->observatoryDockTab(index);
    ASSERT_EQ(tab.name, longName);
    EXPECT_LT(tabBar->tabRect(index).width(), an::ui::observatoryDockTabWidth(tab, false)) << "the tabs had to shrink";
    for (int i = 0; i < tabBar->count(); ++i)
    {
        EXPECT_GE(tabBar->tabRect(i).width(), an::ui::observatoryDockTabWidth(tabBar->observatoryDockTab(i), true))
            << "a tab keeps twelve characters of its name";
    }

    PaintRecorder recorder(tabBar->size());
    recordPainting(tabBar, recorder);
    // Each name keeps both of its ends around the ellipsis.
    QStringList ends;
    for (const auto& drawn : recorder.texts())
    {
        if (!drawn.text.startsWith(QStringLiteral("NGC7")))
        {
            continue;
        }
        EXPECT_TRUE(drawn.text.contains(QChar(0x2026))) << drawn.text.toStdString();
        ends.append(drawn.text.section(QChar(0x2026), -1));
    }
    ASSERT_EQ(ends.size(), 3);
    // The last two characters of the three names.
    for (const QString& suffix : {QStringLiteral("v3"), QStringLiteral("nd"), QStringLiteral("rd")})
    {
        const auto ending =
            std::count_if(ends.cbegin(), ends.cend(), [&suffix](const QString& end) { return end.endsWith(suffix); });
        EXPECT_EQ(ending, 1) << "one name ends with " << suffix.toStdString();
    }

    EXPECT_EQ(tabBar->toolTipAt(tabBar->tabRect(index).center()), longName);
    EXPECT_EQ(tabBar->toolTipAt(QPoint(tabBar->width() + 40, 4)), QString()) << "no tooltip past the tabs";
}

// An adornment set on a dock widget is drawn after its tab's name, widens the tab, says its tooltip, and runs what it
// was given when it is clicked; a click on the name does not run it; cleared, it is gone.
TEST_F(TabStrip, AdornmentIsDrawnAndClickable)
{
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        const TwoTabs tabs = makeTwoTabs();
        AN_TabBar* tabBar = tabBarOf(tabs.first);
        ASSERT_NE(tabBar, nullptr);
        const int plainWidth = tabBar->tabRect(1).width();
        EXPECT_TRUE(tabBar->adornmentRect(1).isEmpty());

        int activations = 0;
        const QString tip = QStringLiteral("Curves Transformation is open on this image. Click to go to the task");
        an::ui::setTabAdornment(tabs.second, {QStringLiteral("eye"), tip, false, [&activations]() { ++activations; }});
        settle();
        ASSERT_NE(an::ui::tabAdornment(tabs.second), nullptr);
        EXPECT_EQ(an::ui::tabAdornment(tabs.second)->toolTip, tip);
        EXPECT_TRUE(tabBar->observatoryDockTab(1).adorned);
        EXPECT_GT(tabBar->tabRect(1).width(), plainWidth) << "the adornment has its room";

        const QRect adornment = tabBar->adornmentRect(1);
        ASSERT_FALSE(adornment.isEmpty());
        const QRect name = an::ui::observatoryDockTabParts(tabBar->tabRect(1), tabBar->observatoryDockTab(1)).name;
        EXPECT_GE(adornment.left(), name.right()) << "drawn after the name";

        const RenderedPicture picture = RenderedPicture::ofWidget(*stackOf(tabs.first), 1.0, t.layers.surface0);
        const QPoint at =
            placeIn(tabBar, stackOf(tabs.first)) + adornment.topLeft() + QPoint(2, adornment.height() / 2);
        expectColour(picture.at(at.x() + 0.5, at.y() + 0.5), t.colors.accentContainer, "the adornment");

        EXPECT_EQ(tabBar->toolTipAt(adornment.center()), tip);
        EXPECT_EQ(tabBar->toolTipAt(name.center()), QStringLiteral("NGC 7000"));

        clickAt(tabBar, name.center());
        EXPECT_EQ(activations, 0) << "a click on the name is the tab's";
        clickAt(tabBar, adornment.center());
        EXPECT_EQ(activations, 1);

        an::ui::clearTabAdornment(tabs.second);
        settle();
        EXPECT_EQ(an::ui::tabAdornment(tabs.second), nullptr);
        EXPECT_TRUE(tabBar->adornmentRect(1).isEmpty());
        EXPECT_FALSE(tabBar->observatoryDockTab(1).adorned);
        EXPECT_EQ(tabBar->tabRect(1).width(), plainWidth);
    }
}

// A tab whose adornment says it is locked has its close button disabled - drawn at 38 % - and, while it is the current
// tab, the strip's buttons too; the other tab's button and, once the other tab is current, the strip's are enabled; a
// lock lifted enables them again.
TEST_F(TabStrip, LockedTabDimsItsButtons)
{
    const auto& t = an::ui::tokens();
    const TwoTabs tabs = makeTwoTabs();
    AN_TabBar* tabBar = tabBarOf(tabs.first);
    QAbstractButton* closeGroup = stripButton(tabs.first, TitleBarButtonType::Close);
    QAbstractButton* floatGroup = stripButton(tabs.first, TitleBarButtonType::Float);
    ASSERT_NE(tabBar, nullptr);
    ASSERT_NE(closeGroup, nullptr);
    ASSERT_NE(floatGroup, nullptr);
    QWidget* firstClose = tabBar->tabButton(0, QTabBar::RightSide);
    QWidget* secondClose = tabBar->tabButton(1, QTabBar::RightSide);
    ASSERT_NE(firstClose, nullptr);
    ASSERT_NE(secondClose, nullptr);
    ASSERT_TRUE(closeGroup->isEnabled());

    // The glyph's middle, enabled, to compare the dimmed one with.
    const auto glyphMiddle = [&t](QWidget* button) {
        return RenderedPicture::ofWidget(*button, 1.0, t.layers.surface1)
            .at(button->width() / 2.0, button->height() / 2.0);
    };
    const QColor closeGroupEnabled = glyphMiddle(closeGroup);
    const QColor tabCloseEnabled = glyphMiddle(firstClose);

    an::ui::setTabAdornment(tabs.first, {QStringLiteral("lock"), QStringLiteral("locked"), true, {}});
    settle();
    EXPECT_FALSE(firstClose->isEnabled());
    EXPECT_TRUE(secondClose->isEnabled());
    EXPECT_FALSE(closeGroup->isEnabled());
    EXPECT_FALSE(floatGroup->isEnabled());
    expectColour(glyphMiddle(closeGroup), layered(closeGroupEnabled, 0.38, t.layers.surface1), "the strip's button", 4);
    expectColour(glyphMiddle(firstClose), layered(tabCloseEnabled, 0.38, t.layers.surface1), "the tab's button", 4);

    tabs.second->setAsCurrentTab();
    settle();
    EXPECT_TRUE(closeGroup->isEnabled()) << "the current tab is not locked";
    EXPECT_TRUE(floatGroup->isEnabled());
    EXPECT_FALSE(firstClose->isEnabled());

    tabs.first->setAsCurrentTab();
    settle();
    EXPECT_FALSE(closeGroup->isEnabled());

    an::ui::clearTabAdornment(tabs.first);
    settle();
    EXPECT_TRUE(firstClose->isEnabled());
    EXPECT_TRUE(closeGroup->isEnabled());
    EXPECT_TRUE(floatGroup->isEnabled());
}

// A strip of more than one tab shows a button that lists them; choosing one makes it current. With one tab it is
// hidden.
TEST_F(TabStrip, TheTabListOffersEveryTab)
{
    const TwoTabs tabs = makeTwoTabs();
    AN_Stack* stack = stackOf(tabs.first);
    ASSERT_NE(stack, nullptr);
    auto* list = stack->findChild<QAbstractButton*>(QStringLiteral("anDockTabList"));
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->isVisible());
    EXPECT_EQ(list->toolTip(), QStringLiteral("Tab List"));

    list->click();
    settle();
    auto* menu = stack->findChild<QMenu*>(QStringLiteral("anDockTabListMenu"));
    ASSERT_NE(menu, nullptr);
    const auto actions = menu->actions();
    ASSERT_EQ(actions.size(), 2);
    EXPECT_EQ(actions[0]->text(), QStringLiteral("M 42"));
    EXPECT_EQ(actions[1]->text(), QStringLiteral("NGC 7000"));
    EXPECT_FALSE(actions[0]->isEnabled()) << "the current tab";
    actions[1]->trigger();
    menu->close();
    settle();
    EXPECT_EQ(stack->currentIndex(), 1);

    tabs.second->close();
    settle();
    EXPECT_FALSE(list->isVisible()) << "nothing to list with one tab";
}

} // namespace an::tests::docking
