// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A switch: a track 32 x 18 and a knob, with an optional label at its right. Off, the track is the fourth surface
// inside a two-pixel outline and a 10 px knob of the secondary text sits at its left; on, the track is the accent and a
// 14 px knob of the text on the accent sits at its right, carrying a check, so the state is never told by colour alone.
// It is toggled by the pointer anywhere on it, by Space and by Enter, and is a check box to assistive technology, named
// by its label.

#include "RenderTestSupport.h"

#include "components/AnSwitch.h"
#include "theme/ComponentParts.h"

#include <QSignalSpy>
#include <QTest>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::ui::AnSwitch;
using an::ui::DesignTokens;
using namespace an::ui::parts;

} // namespace

// A switch is an icon button's row high in both densities and as wide as its track, and its label's gap and width in
// the body text when it has one.
TEST(AnSwitch, SizeFollowsTheDensityAndTheLabel)
{
    forEachLook([](const DesignTokens& t) {
        AnSwitch bare;
        EXPECT_TRUE(bare.isCheckable());
        EXPECT_FALSE(bare.isChecked());
        EXPECT_EQ(bare.sizeHint(), QSize(kSwitchWidth, t.metrics.controlXSmall));

        AnSwitch labelled(QStringLiteral("Night"));
        labelled.resize(labelled.sizeHint());
        const an::ui::testing::RecordedText text = recordedText(labelled, QStringLiteral("Night"));
        ASSERT_EQ(text.text, QStringLiteral("Night"));
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontBody);
        EXPECT_EQ(labelled.sizeHint(),
                  QSize(kSwitchWidth + kSwitchLabelGap + textWidth(text.font, QStringLiteral("Night")),
                        t.metrics.controlXSmall));
        EXPECT_EQ(labelled.minimumSizeHint(), labelled.sizeHint());
        expectColour(text.colour, t.colors.textPrimary, "the label", 0);
    });
}

// Off: the outline, the fourth surface inside it and the small knob at the left. On: the accent and the large knob at
// the right in the text on the accent, with the accent's check on it.
TEST(AnSwitch, PaintsTheTrackAndTheKnobInEachState)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnSwitch toggle;
        toggle.resize(toggle.sizeHint());
        const qreal top = (toggle.height() - kSwitchHeight) / 2.0;
        const qreal middle = top + kSwitchHeight / 2.0;
        forEachRatio(toggle, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(kSwitchWidth / 2.0, top + 1.0),
                         an::ui::compositeOver(t.colors.borderPrimary, an::ui::compositeOver(t.layers.surface4, bg)),
                         "the outline over the track", 24);
            expectColour(picture.at(kSwitchWidth - 6.0, middle), an::ui::compositeOver(t.layers.surface4, bg),
                         "the track off");
            expectColour(picture.at(kSwitchHeight / 2.0, middle), t.colors.textSecondary, "the knob off", 8);
            expectColour(picture.at(0.3, top + 0.3), bg, "the track's rounded end");
        });

        toggle.setChecked(true);
        forEachRatio(toggle, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(5.0, middle), t.colors.accentPrimary, "the track on");
            expectColour(picture.at(kSwitchWidth / 2.0, top + 1.0), t.colors.accentPrimary, "no outline on", 24);
            // The knob's ring, clear of the check at its centre.
            expectColour(picture.at(kSwitchWidth - kSwitchHeight / 2.0, middle - kSwitchKnobOn / 2.0 + 1.5),
                         t.colors.textOnAccent, "the knob on", 24);
        });
    });
}

// The pointer toggles it anywhere on the switch, its label included, and so do Space and Enter.
TEST(AnSwitch, TogglesByThePointerSpaceAndEnter)
{
    AnSwitch toggle(QStringLiteral("Open the last project on startup"));
    toggle.resize(toggle.sizeHint());
    QSignalSpy toggled(&toggle, &QAbstractButton::toggled);
    QTest::mouseClick(&toggle, Qt::LeftButton, Qt::NoModifier, QPoint(toggle.width() - 4, toggle.height() / 2));
    EXPECT_TRUE(toggle.isChecked());
    QTest::keyClick(&toggle, Qt::Key_Space);
    EXPECT_FALSE(toggle.isChecked());
    QTest::keyClick(&toggle, Qt::Key_Return);
    EXPECT_TRUE(toggle.isChecked());
    QTest::keyClick(&toggle, Qt::Key_Enter);
    EXPECT_FALSE(toggle.isChecked());
    EXPECT_EQ(toggled.count(), 4);
}

// A switch is a check box to assistive technology, named by its label, and says when it is on.
TEST(AnSwitch, IsACheckBoxNamedByItsLabel)
{
    AnSwitch toggle(QStringLiteral("Night"));
    EXPECT_EQ(an::tests::widgets::accessibleRole(toggle), QAccessible::CheckBox);
    EXPECT_EQ(an::tests::widgets::accessibleName(toggle), QStringLiteral("Night"));
    toggle.setChecked(true);
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(&toggle);
    ASSERT_NE(iface, nullptr);
    EXPECT_TRUE(iface->state().checked);
}
