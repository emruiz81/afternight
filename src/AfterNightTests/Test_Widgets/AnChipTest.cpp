// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A chip: a pill a chip high with an outline, its text the caption in semibold, an optional icon before it and an
// optional count after it in the monospace. Its toggle form, checked, is the accent's container with no outline. It
// is clicked with the pointer, Space or Enter, draws its focus ring only for the keyboard's focus, and is a button to
// assistive technology - a check box in its toggle form.

#include "RenderTestSupport.h"

#include "components/AnChip.h"
#include "theme/ComponentParts.h"

#include <QFocusEvent>
#include <QIcon>
#include <QPixmap>
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
using an::ui::AnChip;
using an::ui::DesignTokens;

QIcon blankIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    return QIcon(pixmap);
}

} // namespace

// A chip is a chip high in both densities; its width is its padding, its text in the caption, and the icon and the
// count with their gaps when it has them.
TEST(AnChip, SizeFollowsTheDensityAndItsParts)
{
    using namespace an::ui::parts;
    forEachLook([](const DesignTokens& t) {
        AnChip chip(QStringLiteral("Lights"));
        EXPECT_EQ(chip.sizeHint().height(), t.metrics.chip);
        chip.resize(chip.sizeHint());
        const an::ui::testing::RecordedText text = recordedText(chip, QStringLiteral("Lights"));
        ASSERT_EQ(text.text, QStringLiteral("Lights"));
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontCaption);
        EXPECT_EQ(text.font.weight(), QFont::DemiBold);
        const int textWidth = an::ui::parts::textWidth(text.font, QStringLiteral("Lights"));
        EXPECT_EQ(chip.sizeHint().width(), 2 * kChipPadding + textWidth);

        chip.setIcon(blankIcon());
        EXPECT_EQ(chip.sizeHint().width(), kChipPaddingBeforeIcon + kChipIcon + kChipGap + textWidth + kChipPadding);

        chip.setCountText(QStringLiteral("718"));
        EXPECT_EQ(chip.countText(), QStringLiteral("718"));
        chip.resize(chip.sizeHint());
        const an::ui::testing::RecordedText count = recordedText(chip, QStringLiteral("718"));
        ASSERT_EQ(count.text, QStringLiteral("718"));
        EXPECT_EQ(count.font.families(), t.typography.monospace.families());
        EXPECT_EQ(chip.sizeHint().width(), kChipPaddingBeforeIcon + kChipIcon + kChipGap + textWidth + kChipGap +
                                               an::ui::parts::textWidth(count.font, QStringLiteral("718")) +
                                               kChipPadding);
        EXPECT_EQ(chip.minimumSizeHint(), chip.sizeHint());
    });
}

// Unchecked, a chip is an outline on what lies under it with its text in the primary text and its count in the
// secondary; checked, it is the accent's container with no outline and both in the container's text. Its corners are
// half its height.
TEST(AnChip, PaintsItsStates)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnChip chip(QStringLiteral("Grid"));
        chip.setCheckable(true);
        chip.setCountText(QStringLiteral("3"));
        chip.resize(chip.sizeHint());
        const qreal middle = chip.height() / 2.0;
        forEachRatio(chip, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(chip.width() / 2.0, 2.5), bg, "no fill unchecked");
            expectColour(picture.at(chip.width() / 2.0, 0.5), an::ui::compositeOver(t.colors.borderPrimary, bg),
                         "the outline", 24);
            expectColour(picture.at(0.5, 0.5), bg, "the pill's corner");
        });
        expectColour(recordedText(chip, QStringLiteral("Grid")).colour, t.colors.textPrimary, "the text", 0);
        expectColour(recordedText(chip, QStringLiteral("3")).colour, t.colors.textSecondary, "the count", 0);

        chip.setChecked(true);
        const QColor container = an::ui::compositeOver(t.colors.accentContainer, bg);
        forEachRatio(chip, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(chip.width() / 2.0, 1.5), container, "the container");
            expectColour(picture.at(chip.width() / 2.0, 0.5), container, "no outline when checked", 24);
            // Inside the rounded end and left of the text, which starts a padding in: no glyph is under it.
            expectColour(picture.at(an::ui::parts::kChipPadding / 2.0, middle), container, "the pill's end");
        });
        expectColour(recordedText(chip, QStringLiteral("Grid")).colour, t.colors.textOnAccentContainer,
                     "the checked text", 0);
        expectColour(recordedText(chip, QStringLiteral("3")).colour, t.colors.textOnAccentContainer,
                     "the checked count", 0);
    });
}

// A chip is clicked by the pointer, by Space and by Enter; its toggle form changes state and says so.
TEST(AnChip, EmitsWhenClickedByThePointerSpaceOrEnter)
{
    AnChip chip(QStringLiteral("Annotations"));
    chip.resize(chip.sizeHint());
    QSignalSpy clicked(&chip, &QAbstractButton::clicked);
    QTest::mouseClick(&chip, Qt::LeftButton, Qt::NoModifier, chip.rect().center());
    QTest::keyClick(&chip, Qt::Key_Space);
    QTest::keyClick(&chip, Qt::Key_Return);
    QTest::keyClick(&chip, Qt::Key_Enter);
    EXPECT_EQ(clicked.count(), 4);

    chip.setCheckable(true);
    QSignalSpy toggled(&chip, &QAbstractButton::toggled);
    QTest::keyClick(&chip, Qt::Key_Space);
    EXPECT_TRUE(chip.isChecked());
    QTest::keyClick(&chip, Qt::Key_Enter);
    EXPECT_FALSE(chip.isChecked());
    ASSERT_EQ(toggled.count(), 2);
    EXPECT_TRUE(toggled.at(0).at(0).toBool());
}

// The focus ring - two pixels of the focus colour at the chip's edge, as the proxy style draws a control's - is drawn
// when the keyboard brings the focus, not when a click does, and goes with the focus.
TEST(AnChip, DrawsTheFocusRingForTheKeyboardOnly)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnChip chip(QStringLiteral("Stars"));
        chip.resize(chip.sizeHint());
        const qreal ringX = chip.width() / 2.0;
        const QColor outline = an::ui::compositeOver(t.colors.borderPrimary, bg);
        QFocusEvent mouseFocus(QEvent::FocusIn, Qt::MouseFocusReason);
        QApplication::sendEvent(&chip, &mouseFocus);
        expectColour(RenderedPicture::ofWidget(chip, 1.0, bg).at(ringX, 0.5), outline, "no ring for a click", 24);
        QFocusEvent keyboardFocus(QEvent::FocusIn, Qt::TabFocusReason);
        QApplication::sendEvent(&chip, &keyboardFocus);
        forEachRatio(chip, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(ringX, 1.0), t.colors.borderFocus, "the ring", 32);
        });
        QFocusEvent focusOut(QEvent::FocusOut, Qt::TabFocusReason);
        QApplication::sendEvent(&chip, &focusOut);
        expectColour(RenderedPicture::ofWidget(chip, 1.0, bg).at(ringX, 0.5), outline, "no ring once the focus left",
                     24);
    });
}

// A chip is a button to assistive technology, named by its text; its toggle form is a check box.
TEST(AnChip, IsAButtonOrACheckBoxNamedByItsText)
{
    AnChip chip(QStringLiteral("Detected stars"));
    EXPECT_EQ(an::tests::widgets::accessibleRole(chip), QAccessible::Button);
    EXPECT_EQ(an::tests::widgets::accessibleName(chip), QStringLiteral("Detected stars"));
    chip.setCheckable(true);
    EXPECT_EQ(an::tests::widgets::accessibleRole(chip), QAccessible::CheckBox);
}
