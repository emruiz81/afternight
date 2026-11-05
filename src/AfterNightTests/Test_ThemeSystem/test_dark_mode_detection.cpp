// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QColor>
#include <QPalette>

#include "app/ThemeManager.h"
#include "app/UiStyleTokens.h"

TEST(ThemeSystemDarkModeDetection, ResolveThemeColorSchemeDark)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("dark")), Qt::ColorScheme::Dark);
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeColorSchemeLight)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("light")), Qt::ColorScheme::Light);
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeColorSchemeFallbackUnknown)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("auto")), Qt::ColorScheme::Unknown);
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeColorSchemeIgnoresCaseAndWhitespace)
{
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("  LIGHT  ")), Qt::ColorScheme::Light);
    EXPECT_EQ(an::ui::resolveThemeColorScheme(QStringLiteral("DaRk")), Qt::ColorScheme::Dark);
}

TEST(ThemeSystemDarkModeDetection, ResolveMaterialBackdropModeHandlesKnownModes)
{
    EXPECT_EQ(an::ui::resolveMaterialBackdropMode(QStringLiteral("auto")), an::ui::MaterialBackdropMode::Auto);
    EXPECT_EQ(an::ui::resolveMaterialBackdropMode(QStringLiteral("native")),
              an::ui::MaterialBackdropMode::PreferNative);
    EXPECT_EQ(an::ui::resolveMaterialBackdropMode(QStringLiteral("fallback")),
              an::ui::MaterialBackdropMode::ForceFallback);
}

TEST(ThemeSystemDarkModeDetection, ResolveMaterialBackdropModeFallsBackToAuto)
{
    EXPECT_EQ(an::ui::resolveMaterialBackdropMode(QStringLiteral("unknown")), an::ui::MaterialBackdropMode::Auto);
}

TEST(ThemeSystemDarkModeDetection, MaterialBackdropModeIdRoundTrips)
{
    EXPECT_EQ(an::ui::materialBackdropModeId(an::ui::MaterialBackdropMode::Auto), QStringLiteral("auto"));
    EXPECT_EQ(an::ui::materialBackdropModeId(an::ui::MaterialBackdropMode::PreferNative), QStringLiteral("native"));
    EXPECT_EQ(an::ui::materialBackdropModeId(an::ui::MaterialBackdropMode::ForceFallback), QStringLiteral("fallback"));
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeContrastProfileHandlesKnownProfiles)
{
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(QStringLiteral("high")), an::ui::ThemeContrastProfile::High);
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(QStringLiteral("balanced")), an::ui::ThemeContrastProfile::Balanced);
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(QStringLiteral("soft")), an::ui::ThemeContrastProfile::Soft);
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(QStringLiteral("oled")), an::ui::ThemeContrastProfile::Oled);
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeContrastProfileFallsBackToBalanced)
{
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(QStringLiteral("unknown")), an::ui::ThemeContrastProfile::Balanced);
}

TEST(ThemeSystemDarkModeDetection, ThemeContrastProfileIdRoundTrips)
{
    EXPECT_EQ(an::ui::themeContrastProfileId(an::ui::ThemeContrastProfile::High), QStringLiteral("high"));
    EXPECT_EQ(an::ui::themeContrastProfileId(an::ui::ThemeContrastProfile::Balanced), QStringLiteral("balanced"));
    EXPECT_EQ(an::ui::themeContrastProfileId(an::ui::ThemeContrastProfile::Soft), QStringLiteral("soft"));
    EXPECT_EQ(an::ui::themeContrastProfileId(an::ui::ThemeContrastProfile::Oled), QStringLiteral("oled"));
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeAccentFamilyHandlesKnownFamilies)
{
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("blue")), an::ui::ThemeAccentFamily::Blue);
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("teal")), an::ui::ThemeAccentFamily::Teal);
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("amber")), an::ui::ThemeAccentFamily::Amber);
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("amethyst")), an::ui::ThemeAccentFamily::Amethyst);
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("rose")), an::ui::ThemeAccentFamily::Rose);
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeAccentFamilyFallsBackToBlue)
{
    EXPECT_EQ(an::ui::resolveThemeAccentFamily(QStringLiteral("unknown")), an::ui::ThemeAccentFamily::Blue);
}

TEST(ThemeSystemDarkModeDetection, ThemeAccentFamilyIdRoundTrips)
{
    EXPECT_EQ(an::ui::themeAccentFamilyId(an::ui::ThemeAccentFamily::Blue), QStringLiteral("blue"));
    EXPECT_EQ(an::ui::themeAccentFamilyId(an::ui::ThemeAccentFamily::Teal), QStringLiteral("teal"));
    EXPECT_EQ(an::ui::themeAccentFamilyId(an::ui::ThemeAccentFamily::Amber), QStringLiteral("amber"));
    EXPECT_EQ(an::ui::themeAccentFamilyId(an::ui::ThemeAccentFamily::Amethyst), QStringLiteral("amethyst"));
    EXPECT_EQ(an::ui::themeAccentFamilyId(an::ui::ThemeAccentFamily::Rose), QStringLiteral("rose"));
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeSurfaceTintFamilyHandlesKnownFamilies)
{
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("neutral")),
              an::ui::ThemeSurfaceTintFamily::Neutral);
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("ocean")), an::ui::ThemeSurfaceTintFamily::Ocean);
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("mint")), an::ui::ThemeSurfaceTintFamily::Mint);
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("amethyst")),
              an::ui::ThemeSurfaceTintFamily::Amethyst);
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("rose")), an::ui::ThemeSurfaceTintFamily::Rose);
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("amber")), an::ui::ThemeSurfaceTintFamily::Amber);
}

// A surface tint is read by its own name only: an accent's name, or any other, is the default.
TEST(ThemeSystemDarkModeDetection, ResolveThemeSurfaceTintFamilyReadsOnlyItsOwnNames)
{
    for (const QString& name : {QStringLiteral("blue"), QStringLiteral("teal"), QStringLiteral("plum"),
                                QStringLiteral("violet"), QStringLiteral("indigo")})
    {
        EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(name), an::ui::ThemeSurfaceTintFamily::Neutral)
            << name.toStdString();
    }
}

TEST(ThemeSystemDarkModeDetection, ResolveThemeSurfaceTintFamilyFallsBackToNeutral)
{
    EXPECT_EQ(an::ui::resolveThemeSurfaceTintFamily(QStringLiteral("unknown")),
              an::ui::ThemeSurfaceTintFamily::Neutral);
}

TEST(ThemeSystemDarkModeDetection, ThemeSurfaceTintFamilyIdRoundTrips)
{
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Neutral), QStringLiteral("neutral"));
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Ocean), QStringLiteral("ocean"));
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Mint), QStringLiteral("mint"));
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Amethyst), QStringLiteral("amethyst"));
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Rose), QStringLiteral("rose"));
    EXPECT_EQ(an::ui::themeSurfaceTintFamilyId(an::ui::ThemeSurfaceTintFamily::Amber), QStringLiteral("amber"));
}

TEST(ThemeSystemDarkModeDetection, ResolveDarkModeForThemeUsesExplicitSettingsDeterministically)
{
    EXPECT_TRUE(an::ui::resolveDarkModeForTheme(QStringLiteral("dark"), false));
    EXPECT_FALSE(an::ui::resolveDarkModeForTheme(QStringLiteral("light"), true));
}

TEST(ThemeSystemDarkModeDetection, ResolveDarkModeForThemeFallsBackForSystemTheme)
{
    EXPECT_TRUE(an::ui::resolveDarkModeForTheme(QStringLiteral("system"), true));
    EXPECT_FALSE(an::ui::resolveDarkModeForTheme(QStringLiteral("system"), false));
}

TEST(ThemeSystemDarkModeDetection, DetectThemeVariantFromPalette)
{
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(12, 12, 12));
    EXPECT_EQ(an::ui::styling::detectThemeVariant(darkPalette), an::ui::styling::ThemeVariant::Dark);

    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window, QColor(245, 245, 245));
    EXPECT_EQ(an::ui::styling::detectThemeVariant(lightPalette), an::ui::styling::ThemeVariant::Light);
}
