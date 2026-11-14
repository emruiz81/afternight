// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The colour roles the Observatory profile names hold, under the Classic profile, the colour AfterNight 1.1 paints in
// each role, read from where 1.1 has it: the viewer's colour functions, the histogram's well, the semantic roles, the
// channel selectors' dots, and the literals the painters use. A widget shared by both profiles that reads a role paints
// under Classic what it painted.

#include "controls/ImageViewThemeColors.h"
#include "theme/AnDesignTokens.h"
#include "widgets/HistogramWidget.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>

namespace
{

using an::ui::SemanticColorRole;

/// The interface's choices as they were, put back when the case ends.
class ChoicesRestored final
{
public:
    ChoicesRestored()
        : m_backdrop(an::ui::materialBackdropMode()), m_contrast(an::ui::themeContrastProfile()),
          m_accent(an::ui::themeAccentFamily()), m_tint(an::ui::themeSurfaceTintFamily())
    {
    }

    ~ChoicesRestored()
    {
        an::ui::setMaterialBackdropMode(m_backdrop);
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
    }

    ChoicesRestored(const ChoicesRestored&) = delete;
    ChoicesRestored& operator=(const ChoicesRestored&) = delete;

private:
    an::ui::MaterialBackdropMode m_backdrop;
    an::ui::ThemeContrastProfile m_contrast;
    an::ui::ThemeAccentFamily m_accent;
    an::ui::ThemeSurfaceTintFamily m_tint;
};

QColor withAlpha(QColor color, const int alpha)
{
    color.setAlpha(alpha);
    return color;
}

/// The colour a channel selector's dot is painted in: the centre of its icon.
QColor dotColour(const char* icon)
{
    const QImage image(QStringLiteral(":/AfterNight/icons/PNG/%1.png").arg(QString::fromLatin1(icon)));
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}

} // namespace

// Every combination the settings can choose, both themes: each role is what 1.1 paints in it.
TEST(NewTokens, ClassicValuesAreWhatClassicPaints)
{
    ASSERT_EQ(an::ui::styleProfile(), an::ui::StyleProfile::Classic);
    const ChoicesRestored restored;
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);
    const HistogramWidget histogram(nullptr);
    const QColor red = dotColour("red_circle");
    const QColor green = dotColour("green_circle");
    const QColor blue = dotColour("blue_circle");
    ASSERT_TRUE(red.isValid() && green.isValid() && blue.isValid()) << "the channel dots are not in the resources";

    int checked = 0;
    for (const auto contrast : {an::ui::ThemeContrastProfile::High, an::ui::ThemeContrastProfile::Balanced,
                                an::ui::ThemeContrastProfile::Soft, an::ui::ThemeContrastProfile::Oled})
    {
        for (const auto accent :
             {an::ui::ThemeAccentFamily::Blue, an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
              an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose})
        {
            for (const auto tint : {an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeSurfaceTintFamily::Ocean,
                                    an::ui::ThemeSurfaceTintFamily::Mint, an::ui::ThemeSurfaceTintFamily::Amethyst,
                                    an::ui::ThemeSurfaceTintFamily::Rose, an::ui::ThemeSurfaceTintFamily::Amber})
            {
                an::ui::setThemeContrastProfile(contrast);
                an::ui::setThemeAccentFamily(accent);
                an::ui::setThemeSurfaceTintFamily(tint);
                for (const bool dark : {true, false})
                {
                    const auto& c = (dark ? an::ui::resolvedDarkTokens() : an::ui::resolvedLightTokens()).colors;
                    const auto semantic = [dark](const SemanticColorRole role) {
                        return an::ui::semanticColor(dark, role);
                    };
                    const std::string combination =
                        std::to_string(static_cast<int>(contrast)) + " " + std::to_string(static_cast<int>(accent)) +
                        " " + std::to_string(static_cast<int>(tint)) + (dark ? " dark" : " light");
                    SCOPED_TRACE(combination);

                    // The view's background around an image, and the backdrop of its overlays.
                    EXPECT_EQ(c.bgCanvas, an::ui::imageViewScrollAreaWindowColor(dark));
                    EXPECT_EQ(c.scrim, an::ui::imageViewOverlayBackdropColor(dark));
                    // A chip on a thumbnail: the window at 170, or 190 in the light theme (ImageThumbnailWidget).
                    EXPECT_EQ(c.scrimSoft, withAlpha(semantic(SemanticColorRole::SurfaceWindow), dark ? 170 : 190));
                    // The histogram's well, in both themes.
                    EXPECT_EQ(c.bgWell, histogram.getBackgroundColor());
                    // The histogram's coordinates, the one transient message: the surface at 200 under the text.
                    EXPECT_EQ(c.bgSnack, withAlpha(c.bgSurface, 200));
                    EXPECT_EQ(c.textOnSnack, c.textPrimary);
                    // A chosen item's highlight, under the primary text (ImageSelectorControl).
                    EXPECT_EQ(c.accentContainer, semantic(SemanticColorRole::HighlightBackground));
                    EXPECT_EQ(c.textOnAccentContainer, c.textPrimary);
                    // The proxy style: a hovered control's fill, a menu's frame, a slider's groove, a selected row's
                    // fill and outline.
                    EXPECT_EQ(c.stateLayer, c.bgHover);
                    EXPECT_EQ(c.edge, c.borderSubtle);
                    EXPECT_EQ(c.accentTrack, c.borderSubtle);
                    EXPECT_EQ(c.accentSoft, withAlpha(c.accentPrimary, dark ? 116 : 84));
                    EXPECT_EQ(c.accentLine, c.borderFocus);
                    // The lighter accent wash.
                    EXPECT_EQ(c.accentSofter, semantic(SemanticColorRole::AccentSoft));
                    // A selection drawn over the image, and the white edge of its handles.
                    EXPECT_EQ(c.accentOnImage, c.accentPrimary);
                    EXPECT_EQ(c.textOnAccentImage, QColor(255, 255, 255));
                    // The featured card's title and details over its picture (FeaturedFileCard).
                    EXPECT_EQ(c.textOnImage, QColor(255, 255, 255, 240));
                    EXPECT_EQ(c.textOnImageSecondary, QColor(255, 255, 255, 190));
                    // The channel selectors' dots; the luminance has none, and its legend is in the primary text.
                    EXPECT_EQ(c.channelRed, red);
                    EXPECT_EQ(c.channelGreen, green);
                    EXPECT_EQ(c.channelBlue, blue);
                    EXPECT_EQ(c.channelGray, c.textPrimary);
                    // The histogram's curves: the pure primaries, and white for a mono image.
                    EXPECT_EQ(c.plotRed, QColor(Qt::red));
                    EXPECT_EQ(c.plotGreen, QColor(Qt::green));
                    EXPECT_EQ(c.plotBlue, QColor(Qt::blue));
                    EXPECT_EQ(c.plotGray, QColor(Qt::white));
                    ++checked;
                }
            }
        }
    }
    EXPECT_EQ(checked, 240);
}
