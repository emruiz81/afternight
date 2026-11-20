// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A segmented control: segments of equal width that share one outline, a hairline between them; the chosen one is the
// accent's container with a check before its text. It is a control high - a small control or an icon button's row in
// its smaller forms. It is one stop for the keyboard: Left and Right move between the segments, Home and End to the
// first and the last, Space and Enter choose; the pointer chooses a segment by a click. Each segment is a radio button
// to assistive technology, named by its text.

#include "RenderTestSupport.h"

#include "components/AnSegmentedControl.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::ui::AnSegmentedControl;
using an::ui::DesignTokens;
using namespace an::ui::parts;

const QStringList& densities()
{
    static const QStringList segments{QStringLiteral("Compact"), QStringLiteral("Comfortable")};
    return segments;
}

} // namespace

// A control high, a small control high, an icon button's row high; the segments of equal width, each the widest text
// with its padding and the check's room, inside one outline.
TEST(AnSegmentedControl, SizeFollowsTheDensityAndTheWidestSegment)
{
    forEachLook([](const DesignTokens& t) {
        AnSegmentedControl control(densities());
        an::tests::widgets::showOffScreen(control);
        EXPECT_EQ(control.count(), 2);
        EXPECT_EQ(control.sizeHint().height(), t.metrics.control);
        control.resize(control.sizeHint());
        control.setCurrentIndex(1);
        const an::ui::testing::RecordedText text = recordedText(control, QStringLiteral("Comfortable"));
        ASSERT_EQ(text.text, QStringLiteral("Comfortable"));
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontSmall);
        EXPECT_EQ(text.font.weight(), QFont::DemiBold);
        const int widest = (std::max)(textWidth(text.font, QStringLiteral("Compact")),
                                      textWidth(text.font, QStringLiteral("Comfortable")));
        const int segment = 2 * kSegmentPadding + kSegmentCheck + kSegmentCheckGap + widest;
        EXPECT_EQ(control.sizeHint().width(), 2 * segment + 1 + 2);
        EXPECT_EQ(control.minimumSizeHint(), control.sizeHint());
        EXPECT_EQ(control.segmentButton(0)->width(), control.segmentButton(1)->width());

        control.setControlHeight(AnSegmentedControl::ControlHeight::Small);
        EXPECT_EQ(control.sizeHint().height(), t.metrics.controlSmall);
        control.setControlHeight(AnSegmentedControl::ControlHeight::ExtraSmall);
        EXPECT_EQ(control.sizeHint().height(), t.metrics.controlXSmall);
    });
}

// The outline holds every segment and a hairline parts them; the chosen segment is the container, its text the
// container's, the others' the primary text; the outer corners are the controls' radius.
TEST(AnSegmentedControl, PaintsTheChosenSegment)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnSegmentedControl control(densities());
        an::tests::widgets::showOffScreen(control);
        control.setCurrentIndex(0);
        control.resize(control.sizeHint());
        const QRect first = control.segmentButton(0)->geometry();
        const QRect second = control.segmentButton(1)->geometry();
        const QColor container = an::ui::compositeOver(t.colors.accentContainer, bg);
        const QColor outline = an::ui::compositeOver(t.colors.borderPrimary, bg);
        forEachRatio(control, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(first.left() + 3, first.center().y()), container, "the chosen segment");
            expectColour(picture.at(second.right() - 3, second.center().y()), bg, "a segment not chosen");
            expectColour(picture.at(second.center().x(), 0.5), outline, "the outline", 24);
            expectColour(picture.at(second.left() - 0.5, second.center().y()), outline, "the hairline between", 24);
            expectColour(picture.at(0.3, 0.3), bg, "the outer corner");
        });
        expectColour(recordedText(control, QStringLiteral("Compact")).colour, t.colors.textOnAccentContainer,
                     "the chosen text", 0);
        expectColour(recordedText(control, QStringLiteral("Comfortable")).colour, t.colors.textPrimary,
                     "the other text", 0);
    });
}

// A click chooses a segment; Left and Right move between the segments and Space and Enter choose the one moved to;
// Home and End go to the ends. A choice is told once, and setting the index the same way says so too.
TEST(AnSegmentedControl, ChoosesByThePointerAndTheKeyboard)
{
    AnSegmentedControl control(QStringList{QStringLiteral("Soft"), QStringLiteral("Balanced"), QStringLiteral("High"),
                                           QStringLiteral("OLED")});
    control.resize(control.sizeHint());
    EXPECT_EQ(control.currentIndex(), -1);
    EXPECT_EQ(control.focusPolicy(), Qt::StrongFocus);
    QSignalSpy changed(&control, &AnSegmentedControl::currentIndexChanged);

    QTest::mouseClick(control.segmentButton(1), Qt::LeftButton);
    EXPECT_EQ(control.currentIndex(), 1);
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.last().at(0).toInt(), 1);

    QTest::keyClick(&control, Qt::Key_Right);
    EXPECT_EQ(control.focusedSegment(), 2);
    EXPECT_EQ(control.currentIndex(), 1) << "moving does not choose";
    QTest::keyClick(&control, Qt::Key_Space);
    EXPECT_EQ(control.currentIndex(), 2);
    QTest::keyClick(&control, Qt::Key_Right);
    QTest::keyClick(&control, Qt::Key_Right);
    EXPECT_EQ(control.focusedSegment(), 3) << "the last segment is where Right stops";
    QTest::keyClick(&control, Qt::Key_Home);
    EXPECT_EQ(control.focusedSegment(), 0);
    QTest::keyClick(&control, Qt::Key_Return);
    EXPECT_EQ(control.currentIndex(), 0);
    QTest::keyClick(&control, Qt::Key_End);
    QTest::keyClick(&control, Qt::Key_Left);
    EXPECT_EQ(control.focusedSegment(), 2);
    QTest::keyClick(&control, Qt::Key_Enter);
    EXPECT_EQ(control.currentIndex(), 2);
    EXPECT_EQ(changed.count(), 4);

    QTest::keyClick(&control, Qt::Key_Enter);
    EXPECT_EQ(changed.count(), 4) << "choosing the chosen segment again changes nothing";
    control.setCurrentIndex(3);
    EXPECT_EQ(changed.count(), 5);
    EXPECT_EQ(control.focusedSegment(), 3) << "the keyboard starts from the chosen segment";
    control.setCurrentIndex(-1);
    EXPECT_EQ(control.currentIndex(), -1);
    EXPECT_FALSE(control.segmentButton(3)->isChecked());
}

// A segment is a radio button to assistive technology, named by its text and checked when chosen.
TEST(AnSegmentedControl, SegmentsAreRadioButtonsNamedByTheirText)
{
    AnSegmentedControl control(densities());
    control.setAccessibleName(QStringLiteral("Density"));
    control.setCurrentIndex(1);
    EXPECT_EQ(an::tests::widgets::accessibleName(control), QStringLiteral("Density"));
    for (int index = 0; index < control.count(); ++index)
    {
        QAbstractButton* const segment = control.segmentButton(index);
        EXPECT_EQ(an::tests::widgets::accessibleRole(*segment), QAccessible::RadioButton);
        EXPECT_EQ(an::tests::widgets::accessibleName(*segment), densities().at(index));
        QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(segment);
        ASSERT_NE(iface, nullptr);
        EXPECT_EQ(iface->state().checked, index == 1);
        EXPECT_EQ(segment->focusPolicy(), Qt::NoFocus) << "the control is the one stop";
    }
}

// A segment disabled - the OLED contrast while the light theme is chosen - is drawn at the disabled opacity and is not
// chosen: neither a click nor the keyboard's Space on it changes the choice, and Left and Right pass over it.
TEST(AnSegmentedControl, ADisabledSegmentIsDrawnDimAndIsNotChosen)
{
    forEachLook([](const DesignTokens& /*t*/) {
        AnSegmentedControl control(QStringList{QStringLiteral("Soft"), QStringLiteral("Balanced"),
                                               QStringLiteral("High"), QStringLiteral("OLED")});
        an::tests::widgets::showOffScreen(control);
        control.resize(control.sizeHint());
        control.setCurrentIndex(1);
        control.segmentButton(2)->setEnabled(false);
        EXPECT_DOUBLE_EQ(recordedText(control, QStringLiteral("High")).opacity, kDisabledOpacity)
            << "a disabled segment is drawn as an enabled one";
        EXPECT_DOUBLE_EQ(recordedText(control, QStringLiteral("Soft")).opacity, 1.0);

        QSignalSpy changed(&control, &AnSegmentedControl::currentIndexChanged);
        QTest::mouseClick(control.segmentButton(2), Qt::LeftButton);
        EXPECT_EQ(control.currentIndex(), 1);
        QTest::keyClick(&control, Qt::Key_Right);
        EXPECT_EQ(control.focusedSegment(), 3) << "Right stopped on the disabled segment";
        QTest::keyClick(&control, Qt::Key_Left);
        EXPECT_EQ(control.focusedSegment(), 1) << "Left stopped on the disabled segment";
        control.segmentButton(3)->setEnabled(false);
        QTest::keyClick(&control, Qt::Key_End);
        EXPECT_EQ(control.focusedSegment(), 1) << "End went to a disabled segment";
        QTest::keyClick(&control, Qt::Key_Space);
        EXPECT_EQ(control.currentIndex(), 1);
        EXPECT_EQ(changed.count(), 0) << "a disabled segment was chosen";
    });
}
