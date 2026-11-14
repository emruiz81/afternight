// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A breadcrumb: where the user is, as its parts - a place, then an item - with a chevron in the tertiary text between
// them; the parts in the secondary text, the last in the primary text and semibold, all at the small size. A name is
// never shortened to a part of itself: when the room is short the last part is elided in the middle first, then the
// others, and the whole path is the tooltip. A click on a part says which. It is static text to assistive technology,
// read as the whole path.

#include "RenderTestSupport.h"

#include "components/AnBreadcrumb.h"
#include "theme/ComponentParts.h"

#include <QSignalSpy>
#include <QTest>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::ui::AnBreadcrumb;
using an::ui::DesignTokens;
using namespace an::ui::parts;

const QStringList& workspacePath()
{
    static const QStringList parts{QStringLiteral("Workspace 1"), QStringLiteral("stacked_3008x3004_Red_Caldwell_77")};
    return parts;
}

} // namespace

// At the width it asks for, a breadcrumb shows every part whole: its parts, a chevron with a gap at each side between
// two, in their fonts. It is an icon button's row high in both densities.
TEST(AnBreadcrumb, AsksForTheRoomOfEveryPartWhole)
{
    forEachLook([](const DesignTokens& t) {
        AnBreadcrumb crumb;
        an::tests::widgets::showOffScreen(crumb);
        crumb.setParts(workspacePath());
        EXPECT_EQ(crumb.parts(), workspacePath());
        EXPECT_EQ(crumb.sizeHint().height(), t.metrics.controlXSmall);
        crumb.resize(crumb.sizeHint());
        EXPECT_FALSE(crumb.isElided());
        EXPECT_EQ(crumb.shownParts(), workspacePath());
        EXPECT_TRUE(crumb.toolTip().isEmpty());

        const an::ui::testing::RecordedText place = recordedText(crumb, QStringLiteral("Workspace 1"));
        const an::ui::testing::RecordedText item = recordedText(crumb, QStringLiteral("stacked_"));
        ASSERT_EQ(item.text, workspacePath().at(1));
        EXPECT_EQ(logicalPixels(place.font), t.metrics.fontSmall);
        EXPECT_EQ(logicalPixels(item.font), t.metrics.fontSmall);
        EXPECT_EQ(item.font.weight(), QFont::DemiBold);
        expectColour(place.colour, t.colors.textSecondary, "the place", 0);
        expectColour(item.colour, t.colors.textPrimary, "the item", 0);
        EXPECT_EQ(crumb.sizeHint().width(), textWidth(place.font, workspacePath().at(0)) + 2 * kBreadcrumbGap +
                                                kBreadcrumbChevron + textWidth(item.font, workspacePath().at(1)));
    });
}

// Short of room, the item is elided in the middle and keeps both its ends; shorter still, the place is elided too. The
// tooltip is the whole path.
TEST(AnBreadcrumb, ElidesTheItemInTheMiddleFirst)
{
    forEachLook([](const DesignTokens&) {
        AnBreadcrumb crumb;
        an::tests::widgets::showOffScreen(crumb);
        crumb.setParts(workspacePath());
        crumb.resize(crumb.sizeHint().width() - 80, crumb.sizeHint().height());
        ASSERT_TRUE(crumb.isElided());
        QStringList shown = crumb.shownParts();
        ASSERT_EQ(shown.size(), 2);
        EXPECT_EQ(shown.at(0), workspacePath().at(0));
        EXPECT_TRUE(shown.at(1).contains(QChar(0x2026))) << shown.at(1).toStdString();
        EXPECT_TRUE(shown.at(1).startsWith(QStringLiteral("st"))) << shown.at(1).toStdString();
        EXPECT_TRUE(shown.at(1).endsWith(QStringLiteral("77"))) << shown.at(1).toStdString();
        EXPECT_EQ(crumb.toolTip(), QStringLiteral("Workspace 1 > stacked_3008x3004_Red_Caldwell_77"));

        crumb.resize(crumb.minimumSizeHint().width(), crumb.height());
        shown = crumb.shownParts();
        EXPECT_TRUE(shown.at(0).contains(QChar(0x2026))) << shown.at(0).toStdString();

        crumb.resize(crumb.sizeHint());
        EXPECT_FALSE(crumb.isElided());
        EXPECT_TRUE(crumb.toolTip().isEmpty());
    });
}

// A click on a part tells which part; a click on a chevron tells nothing.
TEST(AnBreadcrumb, AClickOnAPartSaysWhich)
{
    AnBreadcrumb crumb;
    crumb.setParts(QStringList{QStringLiteral("Image Integration"), QStringLiteral("Carina Nebula LRGB")});
    crumb.resize(crumb.sizeHint());
    QSignalSpy activated(&crumb, &AnBreadcrumb::partActivated);
    QTest::mouseClick(&crumb, Qt::LeftButton, Qt::NoModifier, QPoint(2, crumb.height() / 2));
    QTest::mouseClick(&crumb, Qt::LeftButton, Qt::NoModifier, QPoint(crumb.width() - 2, crumb.height() / 2));
    ASSERT_EQ(activated.count(), 2);
    EXPECT_EQ(activated.at(0).at(0).toInt(), 0);
    EXPECT_EQ(activated.at(1).at(0).toInt(), 1);
}

// Assistive technology reads a breadcrumb as static text: the whole path.
TEST(AnBreadcrumb, IsStaticTextReadAsTheWholePath)
{
    AnBreadcrumb crumb;
    crumb.setParts(workspacePath());
    EXPECT_EQ(an::tests::widgets::accessibleRole(crumb), QAccessible::StaticText);
    EXPECT_EQ(an::tests::widgets::accessibleName(crumb),
              QStringLiteral("Workspace 1 > stacked_3008x3004_Red_Caldwell_77"));
    crumb.setParts(QStringList{QStringLiteral("Home")});
    EXPECT_EQ(an::tests::widgets::accessibleName(crumb), QStringLiteral("Home"));
}
