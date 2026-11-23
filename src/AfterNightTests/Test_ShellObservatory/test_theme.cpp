// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QColor>
#include "app/ThemeManager.h"
#include "theme/AnDesignTokens.h"

TEST(AfterNightAppThemeSuite, ResolveThemeColorSchemeHandlesDark)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("dark")), Qt::ColorScheme::Dark);
}

TEST(AfterNightAppThemeSuite, ResolveThemeColorSchemeHandlesLight)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("light")), Qt::ColorScheme::Light);
}

TEST(AfterNightAppThemeSuite, ResolveThemeColorSchemeFallsBackToSystem)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("system")), Qt::ColorScheme::Unknown);
}

TEST(AfterNightAppThemeSuite, IconPaletteForDarkModeUsesExpectedColors)
{
    const auto [normal, disabled] = an::ui::iconPaletteForTheme(true);
    EXPECT_EQ(normal, an::ui::resolvedDarkTokens().colors.textPrimary);
    EXPECT_EQ(disabled, an::ui::resolvedDarkTokens().colors.textTertiary);
}

TEST(AfterNightAppThemeSuite, IconPaletteForLightModeUsesExpectedColors)
{
    const auto [normal, disabled] = an::ui::iconPaletteForTheme(false);
    EXPECT_EQ(normal, an::ui::resolvedLightTokens().colors.textPrimary);
    EXPECT_EQ(disabled, an::ui::resolvedLightTokens().colors.textTertiary);
}
