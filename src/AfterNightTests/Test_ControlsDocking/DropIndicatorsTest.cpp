// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The drop indicators under the Observatory profile, the product's own window made through the application's view
// factory: a cross of five targets over the group under the cursor and four at the dock area's edges, each a raised
// square whose filled half says where the view would land; while the cursor is over one, the band - the accent's soft
// share inside a two-pixel accent border - lies over that place.

#include "ObservatoryDockingSupport.h"

#include "controls/DockDropIndicators.h"

#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>

#include <array>
#include <utility>

namespace an::tests::docking
{

namespace
{

using KDDockWidgets::DropLocation;

/// A rectangle on the screen.
QRect globalRectOf(const QWidget* widget)
{
    return {widget->mapToGlobal(QPoint(0, 0)), widget->size()};
}

/// Whether a band lies where a target says the view would land: on that side of the group under the cursor, or at that
/// edge of the whole dock area. How much room the view would take is the library's to say.
void expectBandOnItsSide(const DropLocation location, const QRect& band, const QRect& group, const QRect& area)
{
    constexpr int slack = 3;
    const QPoint middle = band.center();
    switch (location)
    {
    case KDDockWidgets::DropLocation_Center:
        EXPECT_TRUE(band.contains(group.center()));
        EXPECT_GE(band.width(), group.width() * 3 / 4);
        EXPECT_GE(band.height(), group.height() * 3 / 4);
        break;
    case KDDockWidgets::DropLocation_Left:
        EXPECT_LE(band.left(), group.left() + slack);
        EXPECT_LT(middle.x(), group.center().x());
        break;
    case KDDockWidgets::DropLocation_Right:
        EXPECT_GT(middle.x(), group.center().x());
        EXPECT_GE(band.right(), group.right() - slack);
        break;
    case KDDockWidgets::DropLocation_Top:
        EXPECT_LE(band.top(), group.top() + slack);
        EXPECT_LT(middle.y(), group.center().y());
        break;
    case KDDockWidgets::DropLocation_Bottom:
        EXPECT_GE(band.bottom(), group.bottom() - slack);
        EXPECT_GT(middle.y(), group.center().y());
        break;
    case KDDockWidgets::DropLocation_OutterLeft:
        EXPECT_LE(band.left(), area.left() + slack);
        EXPECT_LT(middle.x(), area.center().x());
        break;
    case KDDockWidgets::DropLocation_OutterRight:
        EXPECT_GE(band.right(), area.right() - slack);
        EXPECT_GT(middle.x(), area.center().x());
        break;
    case KDDockWidgets::DropLocation_OutterTop:
        EXPECT_LE(band.top(), area.top() + slack);
        EXPECT_LT(middle.y(), area.center().y());
        break;
    case KDDockWidgets::DropLocation_OutterBottom:
        EXPECT_GE(band.bottom(), area.bottom() - slack);
        EXPECT_GT(middle.y(), area.center().y());
        break;
    default:
        ADD_FAILURE() << "not a target";
        break;
    }
}

constexpr std::array<std::pair<DropLocation, const char*>, 9> kTargets = {{
    {KDDockWidgets::DropLocation_Center, "centre"},
    {KDDockWidgets::DropLocation_Left, "left"},
    {KDDockWidgets::DropLocation_Top, "top"},
    {KDDockWidgets::DropLocation_Right, "right"},
    {KDDockWidgets::DropLocation_Bottom, "bottom"},
    {KDDockWidgets::DropLocation_OutterLeft, "the area's left"},
    {KDDockWidgets::DropLocation_OutterTop, "the area's top"},
    {KDDockWidgets::DropLocation_OutterRight, "the area's right"},
    {KDDockWidgets::DropLocation_OutterBottom, "the area's bottom"},
}};

} // namespace

class DropIndicators : public ObservatoryDockingTest
{
protected:
    /// A workspace with two docked views side by side and a floating one to drag over them.
    void makeDockArea()
    {
        m_workspaces = makeWorkspaceStack();
        m_workspaces.window->setGeometry(20, 20, 640, 480);
        settle();
        m_left = makeView(QStringLiteral("left"));
        m_right = makeView(QStringLiteral("right"));
        m_floating = makeView(QStringLiteral("floating"));
        m_workspaces.first->addDockWidget(m_left, KDDockWidgets::Location_OnLeft);
        m_workspaces.first->addDockWidget(m_right, KDDockWidgets::Location_OnRight);
        floatAt(m_floating, QRect(720, 40, 240, 200));
        m_indicators =
            dynamic_cast<KDDockWidgets::Core::ClassicDropIndicatorOverlay*>(indicatorsOf(m_workspaces.first));
    }

    /// Takes the dock area away, so that the next one is the only one under the cursor.
    void clearDockArea()
    {
        for (DockWidget* view : {m_floating, m_left, m_right})
        {
            delete view;
        }
        settle();
        delete m_workspaces.window;
        settle();
        m_indicators = nullptr;
    }

    WorkspaceStack m_workspaces;
    DockWidget* m_left = nullptr;
    DockWidget* m_right = nullptr;
    DockWidget* m_floating = nullptr;
    KDDockWidgets::Core::ClassicDropIndicatorOverlay* m_indicators = nullptr;
};

// Under the profile the dock area's indicator window and band are the product's.
TEST_F(DropIndicators, TheFactoryMakesTheProductsIndicatorsAndBand)
{
    makeDockArea();
    ASSERT_NE(m_indicators, nullptr);
    EXPECT_NE(dynamic_cast<AN_DropIndicators*>(m_indicators->indicatorWindow()), nullptr);
    EXPECT_NE(dynamic_cast<AN_DropBand*>(widgetOf(m_indicators->rubberBand())), nullptr);
}

// Each of the nine targets is offered where it stands for - the cross over the group under the cursor, a pitch apart,
// the four others at the area's edges - and, the cursor over it, the band lies over the place the view would take.
TEST_F(DropIndicators, NineTargetsEachLandOnTheirSide)
{
    makeDockArea();
    ASSERT_NE(m_indicators, nullptr);
    auto* window = dynamic_cast<AN_DropIndicators*>(m_indicators->indicatorWindow());
    ASSERT_NE(window, nullptr);
    QWidget* band = widgetOf(m_indicators->rubberBand());
    ASSERT_NE(band, nullptr);

    auto drag = SimulatedDrag::ofFloatingWindow(m_floating);
    const QPoint overLeft = m_left->mapToGlobal(m_left->rect().center());
    drag.moveTo(overLeft);
    ASSERT_TRUE(m_indicators->isHovered());
    EXPECT_TRUE(window->isVisible());

    const int side = an::ui::observatoryDropTargetSide();
    const int pitch = an::ui::observatoryDropTargetPitch();
    const QRect centre = window->targetRect(KDDockWidgets::DropLocation_Center);
    EXPECT_EQ(centre.size(), QSize(side, side));
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_Left).center(), centre.center() - QPoint(pitch, 0));
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_Right).center(), centre.center() + QPoint(pitch, 0));
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_Top).center(), centre.center() - QPoint(0, pitch));
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_Bottom).center(), centre.center() + QPoint(0, pitch));
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_OutterLeft).left(), an::ui::observatoryDropTargetMargin());
    EXPECT_EQ(window->targetRect(KDDockWidgets::DropLocation_OutterTop).top(), an::ui::observatoryDropTargetMargin());

    // The group the cross stands over is the left view's; the outer targets stand for the whole area.
    const QRect leftGroup = globalRectOf(widgetOf(m_left->group()->view()));
    const QRect area = globalRectOf(m_workspaces.first);
    for (const auto& [location, name] : kTargets)
    {
        SCOPED_TRACE(name);
        // Not an assertion: the drag must be released below whatever a target shows.
        EXPECT_TRUE(drag.moveOnto(m_indicators, overLeft, location)) << "the target is offered and hovered";
        EXPECT_TRUE(band->isVisible());
        expectBandOnItsSide(location, globalRectOf(band), leftGroup, area);
    }

    drag.release();
    EXPECT_FALSE(window->isVisible());
    EXPECT_FALSE(band->isVisible());
    EXPECT_FALSE(m_floating->isFloating()) << "released over the last target, the view is docked";
}

// A target is a square of the third surface whose half is filled with the accent's line colour, or with the accent
// while the cursor is over it; the band is the accent's soft share inside a two-pixel accent border.
TEST_F(DropIndicators, ObservatoryTargetsAndBandAreDrawnFromTheTokens)
{
    for (const ObservatoryLook& look : {observatoryLooks()[0], observatoryLooks()[1], observatoryLooks()[2]})
    {
        SCOPED_TRACE(look.name);
        setLook(look);
        const auto& t = an::ui::tokens();
        makeDockArea();
        ASSERT_NE(m_indicators, nullptr);
        auto* window = dynamic_cast<AN_DropIndicators*>(m_indicators->indicatorWindow());
        ASSERT_NE(window, nullptr);

        auto drag = SimulatedDrag::ofFloatingWindow(m_floating);
        const QPoint overLeft = m_left->mapToGlobal(m_left->rect().center());
        EXPECT_TRUE(drag.moveOnto(m_indicators, overLeft, KDDockWidgets::DropLocation_Left));

        const RenderedPicture picture = RenderedPicture::ofWidget(*window, 1.0, t.layers.surface0);
        const auto halfOf = [](const QRect& target, const an::ui::ObservatoryDropSide half) {
            return an::ui::observatoryDropTargetHalf(target, half);
        };
        const QRect top = window->targetRect(KDDockWidgets::DropLocation_Top);
        const QRect topHalf = halfOf(top, an::ui::ObservatoryDropSide::Top);
        const QRect bottomHalf = halfOf(top, an::ui::ObservatoryDropSide::Bottom);
        expectColour(picture.at(topHalf.center().x() + 0.5, topHalf.center().y() + 0.5),
                     an::ui::compositeOver(t.colors.accentLine, t.layers.surface3), "the half a target stands for");
        expectColour(picture.at(bottomHalf.center().x() + 0.5, bottomHalf.center().y() + 0.5), t.layers.surface3,
                     "the other half");
        expectColour(picture.at(top.left() + 0.5, top.center().y() + 0.5),
                     an::ui::compositeOver(t.colors.borderPrimary, t.layers.surface3), "the outline", 6);
        const QRect hot =
            halfOf(window->targetRect(KDDockWidgets::DropLocation_Left), an::ui::ObservatoryDropSide::Left);
        expectColour(picture.at(hot.center().x() + 0.5, hot.center().y() + 0.5), t.colors.accentPrimary,
                     "the target under the cursor");

        QWidget* band = widgetOf(m_indicators->rubberBand());
        const RenderedPicture bandPicture = RenderedPicture::ofWidget(*band, 1.0, t.layers.surface0);
        expectColour(bandPicture.at(0.5, band->height() / 2.0), t.colors.accentPrimary, "the band's border");
        expectColour(bandPicture.at(1.5, band->height() / 2.0), t.colors.accentPrimary, "two pixels of it");
        expectColour(bandPicture.at(band->width() / 2.0, band->height() / 2.0),
                     an::ui::compositeOver(t.colors.accentSoft, t.layers.surface0), "the band");

        drag.moveTo(QPoint(1200, 700));
        drag.release();
        clearDockArea();
    }
}

} // namespace an::tests::docking
