// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A tag: a short text on a small square-cornered fill of its tone - a tag high, its corners 4 px, its text the caption
// in semibold - in the neutral, the accent's and the four status tones; a warning is outlined too, so that it is never
// told by its hue alone. It is static text to assistive technology, read by its text.

#include "RenderTestSupport.h"

#include "components/AnTag.h"
#include "theme/ComponentParts.h"

#include <QIcon>
#include <QPixmap>

#include <array>
#include <utility>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::recordedText;
using an::ui::AnTag;
using an::ui::DesignTokens;

/// The fill and the text each tone is drawn in.
std::pair<QColor, QColor> toneColours(const AnTag::Tone tone, const DesignTokens& t)
{
    switch (tone)
    {
    case AnTag::Tone::Accent:
        return {t.colors.accentContainer, t.colors.textOnAccentContainer};
    case AnTag::Tone::Success:
        return {t.colors.statusSuccessSoft, t.colors.statusSuccess};
    case AnTag::Tone::Warning:
        return {t.colors.statusWarningSoft, t.colors.statusWarning};
    case AnTag::Tone::Error:
        return {t.colors.statusErrorSoft, t.colors.statusError};
    case AnTag::Tone::Info:
        return {t.colors.statusInfoSoft, t.colors.statusInfo};
    case AnTag::Tone::Neutral:
    default:
        return {t.layers.surface4, t.colors.textSecondary};
    }
}

constexpr std::array<AnTag::Tone, 6u> kTones{AnTag::Tone::Neutral, AnTag::Tone::Accent, AnTag::Tone::Success,
                                             AnTag::Tone::Warning, AnTag::Tone::Error,  AnTag::Tone::Info};

} // namespace

// A tag is a tag high in both densities and as wide as its text in the caption, semibold, with its padding at both
// sides; an icon adds its size and a gap.
TEST(AnTag, SizeFollowsTheDensityAndTheText)
{
    forEachLook([](const DesignTokens& t) {
        AnTag tag(QStringLiteral("Masters"));
        EXPECT_EQ(tag.sizeHint().height(), t.metrics.tag);
        EXPECT_EQ(tag.minimumSizeHint(), tag.sizeHint());
        tag.resize(tag.sizeHint());
        const an::ui::testing::RecordedText text = recordedText(tag, QStringLiteral("Masters"));
        ASSERT_EQ(text.text, QStringLiteral("Masters"));
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontCaption);
        EXPECT_EQ(text.font.weight(), QFont::DemiBold);
        EXPECT_EQ(tag.sizeHint().width(),
                  2 * an::ui::parts::kTagPadding + an::ui::parts::textWidth(text.font, QStringLiteral("Masters")));

        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        tag.setIcon(QIcon(pixmap));
        EXPECT_EQ(tag.sizeHint().width(), 2 * an::ui::parts::kTagPadding + an::ui::parts::kTagIcon +
                                              an::ui::parts::kTagGap +
                                              an::ui::parts::textWidth(text.font, QStringLiteral("Masters")));
    });
}

// Each tone is its fill under its text, the corners 4 px; a warning carries an outline of its tone too.
TEST(AnTag, TonesPaintTheirFillTextAndCorners)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface2;
        for (const AnTag::Tone tone : kTones)
        {
            SCOPED_TRACE(static_cast<int>(tone));
            AnTag tag(QStringLiteral("Tag"), tone);
            EXPECT_EQ(tag.tone(), tone);
            tag.resize(tag.sizeHint() + QSize(20, 0));
            const auto [fill, text] = toneColours(tone, t);
            const QColor shownFill = an::ui::compositeOver(fill, bg);
            forEachRatio(tag, bg, [&](const an::tests::widgets::RenderedPicture& picture) {
                expectColour(picture.at(4, tag.height() / 2.0), shownFill, "the fill");
                expectColour(picture.at(0.2, 0.2), bg, "the corner");
                expectColour(picture.at(2.5, 2.5), shownFill, "inside the corner", 6);
                if (tone == AnTag::Tone::Warning)
                {
                    expectColour(picture.at(0.5, tag.height() / 2.0), an::ui::compositeOver(text, bg),
                                 "the warning's outline", 24);
                }
                else
                {
                    expectColour(picture.at(0.5, tag.height() / 2.0), shownFill, "no outline");
                }
            });
            expectColour(recordedText(tag, QStringLiteral("Tag")).colour, text, "the text", 0);
        }
    });
}

// A tag tells assistive technology it is static text named by its text, and its text and tone can change.
TEST(AnTag, IsStaticTextNamedByItsText)
{
    AnTag tag(QStringLiteral("Calibrated"), AnTag::Tone::Success);
    EXPECT_EQ(an::tests::widgets::accessibleRole(tag), QAccessible::StaticText);
    EXPECT_EQ(an::tests::widgets::accessibleName(tag), QStringLiteral("Calibrated"));
    tag.setText(QStringLiteral("Failed"));
    tag.setTone(AnTag::Tone::Error);
    EXPECT_EQ(tag.tone(), AnTag::Tone::Error);
    EXPECT_EQ(an::tests::widgets::accessibleName(tag), QStringLiteral("Failed"));
}
