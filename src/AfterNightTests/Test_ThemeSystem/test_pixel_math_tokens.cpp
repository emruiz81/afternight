// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QColor>

#include <algorithm>

#include "app/UiStyleTokens.h"
#include "controls/PixelMathThemeTokens.h"

namespace
{
QColor blend(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  lerp(base.alpha(), tint.alpha()));
}

} // anonymous namespace

TEST(PixelMathThemeTokens, SyntaxPaletteUsesSemanticRolesDark)
{
    const auto palette = an::ui::pixelMathSyntaxPalette(true);

    EXPECT_EQ(palette.functionColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                    an::ui::styling::SemanticColorRole::AccentPrimary));
    EXPECT_EQ(palette.specialFunctionColor,
              an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                             an::ui::styling::SemanticColorRole::AccentHover));
    EXPECT_EQ(palette.imageColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                 an::ui::styling::SemanticColorRole::StatusSuccess));
    EXPECT_EQ(palette.symbolColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                  an::ui::styling::SemanticColorRole::StatusActive));
    EXPECT_EQ(palette.commentColor,
              blend(an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::StatusSuccess),
                    an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                   an::ui::styling::SemanticColorRole::TextSecondary),
                    0.35));
}

TEST(PixelMathThemeTokens, SyntaxPaletteUsesSemanticRolesLight)
{
    const auto palette = an::ui::pixelMathSyntaxPalette(false);

    EXPECT_EQ(palette.functionColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                                                    an::ui::styling::SemanticColorRole::AccentPrimary));
    EXPECT_EQ(palette.imageColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                                                 an::ui::styling::SemanticColorRole::StatusSuccess));
    EXPECT_EQ(palette.operatorColor, an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                                                    an::ui::styling::SemanticColorRole::TextPrimary));
}
