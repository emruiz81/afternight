// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A key hint: the keys of a shortcut, separated by spaces, in the monospace at the caption's size, in a small box of
// the second surface with a hairline and 4 px corners; on a coloured part it is an outline of the text's colour with no
// fill. It is a tag less two pixels high, and static text to assistive technology, read as the shortcut.

#include "RenderTestSupport.h"

#include "components/AnKeyHint.h"
#include "theme/ComponentParts.h"

#include <QKeySequence>
#include <QPalette>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::ui::AnKeyHint;
using an::ui::DesignTokens;

} // namespace

// The keys are written as the user presses them: the modifiers and the key, separated by spaces.
TEST(AnKeyHint, WritesTheKeysSeparatedBySpaces)
{
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::CTRL | Qt::Key_K)), QStringLiteral("Ctrl K"));
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::Key_Escape)), QStringLiteral("Esc"));
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::CTRL | Qt::Key_Enter)), QStringLiteral("Ctrl Enter"));
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)), QStringLiteral("Ctrl Shift N"));
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::CTRL | Qt::Key_Plus)), QStringLiteral("Ctrl +"));
    EXPECT_EQ(AnKeyHint::keysText(QKeySequence(Qt::Key_Space)), QStringLiteral("Space"));

    AnKeyHint hint(QKeySequence(Qt::CTRL | Qt::Key_K));
    EXPECT_EQ(hint.keySequence(), QKeySequence(Qt::CTRL | Qt::Key_K));
    EXPECT_EQ(hint.text(), QStringLiteral("Ctrl K"));
    hint.setKeySequence(QKeySequence(Qt::Key_F1));
    EXPECT_EQ(hint.text(), QStringLiteral("F1"));
}

// A key hint is two pixels less than a tag high in both densities and as wide as its text in the monospace with its
// padding and its hairlines.
TEST(AnKeyHint, SizeFollowsTheDensityAndTheKeys)
{
    forEachLook([](const DesignTokens& t) {
        AnKeyHint hint(QKeySequence(Qt::CTRL | Qt::Key_K));
        EXPECT_EQ(hint.sizeHint().height(), t.metrics.tag - 2);
        EXPECT_EQ(hint.minimumSizeHint(), hint.sizeHint());
        hint.resize(hint.sizeHint());
        const an::ui::testing::RecordedText text = recordedText(hint, QStringLiteral("Ctrl K"));
        ASSERT_EQ(text.text, QStringLiteral("Ctrl K"));
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontCaption);
        EXPECT_EQ(text.font.styleHint(), t.typography.monospace.styleHint());
        EXPECT_EQ(text.font.families(), t.typography.monospace.families());
        EXPECT_EQ(hint.sizeHint().width(), 2 * an::ui::parts::kKeyHintPadding + 2 +
                                               an::ui::parts::textWidth(text.font, QStringLiteral("Ctrl K")));
    });
}

// Raised, a key hint is the second surface inside a hairline, its corners 4 px, its keys in the secondary text; on a
// coloured part it has no fill, and its outline and keys take the text colour its palette gives.
TEST(AnKeyHint, PaintsItsBoxInBothSurfaces)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface1;
        AnKeyHint hint(QKeySequence(Qt::CTRL | Qt::Key_Enter));
        EXPECT_EQ(hint.surface(), AnKeyHint::Surface::Raised);
        hint.resize(hint.sizeHint() + QSize(10, 0));
        forEachRatio(hint, bg, [&](const an::tests::widgets::RenderedPicture& picture) {
            expectColour(picture.at(3, hint.height() / 2.0), an::ui::compositeOver(t.layers.surface2, bg), "the fill");
            expectColour(picture.at(0.5, hint.height() / 2.0),
                         an::ui::compositeOver(t.colors.borderSubtle, an::ui::compositeOver(t.layers.surface2, bg)),
                         "the hairline", 24);
            expectColour(picture.at(0.2, 0.2), bg, "the corner");
        });
        expectColour(recordedText(hint, QStringLiteral("Ctrl")).colour, t.colors.textSecondary, "the keys", 0);

        const QColor onAccent = t.colors.textOnAccent;
        QPalette palette = hint.palette();
        palette.setColor(QPalette::WindowText, onAccent);
        hint.setPalette(palette);
        hint.setSurface(AnKeyHint::Surface::OnColour);
        const QColor accent = t.colors.accentPrimary;
        forEachRatio(hint, accent, [&](const an::tests::widgets::RenderedPicture& picture) {
            expectColour(picture.at(3, hint.height() / 2.0), accent, "no fill");
            expectColour(
                picture.at(0.5, hint.height() / 2.0),
                an::ui::compositeOver(an::ui::parts::atShare(onAccent, an::ui::parts::kKeyHintOnColourOutline), accent),
                "the outline in the text's colour", 24);
        });
        expectColour(recordedText(hint, QStringLiteral("Ctrl")).colour, onAccent, "the keys on a colour", 0);
    });
}

// Assistive technology reads a key hint as static text naming the shortcut as the platform writes it.
TEST(AnKeyHint, IsStaticTextNamedByTheShortcut)
{
    AnKeyHint hint(QKeySequence(Qt::CTRL | Qt::Key_K));
    EXPECT_EQ(an::tests::widgets::accessibleRole(hint), QAccessible::StaticText);
    EXPECT_EQ(an::tests::widgets::accessibleName(hint),
              QKeySequence(Qt::CTRL | Qt::Key_K).toString(QKeySequence::NativeText));
}
