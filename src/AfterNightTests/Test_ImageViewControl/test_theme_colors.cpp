// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QColor>

#include <algorithm>
#include <cmath>

#include "app/UiStyleTokens.h"
#include "controls/ImageViewThemeColors.h"
#include "theme/AnDesignTokens.h"

namespace
{
QColor blend(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

QColor withAlpha(QColor color, const int alpha)
{
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

QColor composite(const QColor& foreground, const QColor& background)
{
    const double alpha = std::clamp(static_cast<double>(foreground.alpha()) / 255.0, 0.0, 1.0);
    const auto blendChannel = [alpha](const int fg, const int bg) {
        return static_cast<int>(std::round(fg * alpha + bg * (1.0 - alpha)));
    };

    return QColor(blendChannel(foreground.red(), background.red()),
                  blendChannel(foreground.green(), background.green()),
                  blendChannel(foreground.blue(), background.blue()), 255);
}

} // anonymous namespace

TEST(ImageViewControlThemeColors, ScrollAreaColorUsesSemanticBlend)
{
    const QColor darkExpected = blend(an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                     an::ui::styling::SemanticColorRole::SurfaceWindow),
                                      an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                     an::ui::styling::SemanticColorRole::SurfaceBorder),
                                      0.22);

    const QColor lightExpected =
        blend(an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                             an::ui::styling::SemanticColorRole::SurfaceWindow),
              an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                             an::ui::styling::SemanticColorRole::SurfaceBorder),
              0.52);

    EXPECT_EQ(an::ui::imageViewScrollAreaWindowColor(true), darkExpected);
    EXPECT_EQ(an::ui::imageViewScrollAreaWindowColor(false), lightExpected);
}

TEST(ImageViewControlThemeColors, OverlayTextColorsUseSubtleSemanticStatusBlends)
{
    const QColor darkTextPrimary = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                  an::ui::styling::SemanticColorRole::TextPrimary);
    const QColor lightTextPrimary = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                                                   an::ui::styling::SemanticColorRole::TextPrimary);

    EXPECT_EQ(an::ui::imageViewOverlayAutostretchColor(true),
              blend(darkTextPrimary,
                    an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::StatusWarning),
                    0.58));
    EXPECT_EQ(an::ui::imageViewOverlayAutostretchColor(false),
              blend(darkTextPrimary,
                    an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::StatusWarning),
                    0.58));

    EXPECT_EQ(an::ui::imageViewOverlayOriginalColor(true),
              blend(darkTextPrimary,
                    an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::StatusError),
                    0.62));
    EXPECT_EQ(an::ui::imageViewOverlayOriginalColor(false),
              blend(darkTextPrimary,
                    an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::StatusError),
                    0.62));
}

TEST(ImageViewControlThemeColors, OverlayBackdropUsesTokenizedAlphaSurface)
{
    const QColor darkExpected =
        withAlpha(an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                 an::ui::styling::SemanticColorRole::SurfaceWindow),
                  170);

    const QColor lightExpected =
        withAlpha(an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                 an::ui::styling::SemanticColorRole::SurfaceWindow),
                  170);

    EXPECT_EQ(an::ui::imageViewOverlayBackdropColor(true), darkExpected);
    EXPECT_EQ(an::ui::imageViewOverlayBackdropColor(false), lightExpected);
}

TEST(ImageViewControlThemeColors, CardSurfaceTintMatchesToolbarAndCardBlendFormula)
{
    const auto expectedSurfaceColor = [](const an::ui::DesignTokens& tokenSet, const bool darkMode) {
        QColor surface = tokenSet.materials.acrylicSurface;
        if (tokenSet.colors.cardSurfaceTint.isValid())
        {
            surface = blend(surface, tokenSet.colors.cardSurfaceTint, darkMode ? 0.56 : 0.44);
            if (darkMode)
            {
                surface = blend(surface, QColor(0, 0, 0), 0.22);
            }
        }

        if (surface.alpha() < 255)
        {
            surface = composite(surface, tokenSet.materials.micaSurface);
        }

        return surface;
    };

    EXPECT_EQ(an::ui::imageViewCardSurfaceTintColor(true), expectedSurfaceColor(an::ui::resolvedDarkTokens(), true));
    EXPECT_EQ(an::ui::imageViewCardSurfaceTintColor(false), expectedSurfaceColor(an::ui::resolvedLightTokens(), false));
}
