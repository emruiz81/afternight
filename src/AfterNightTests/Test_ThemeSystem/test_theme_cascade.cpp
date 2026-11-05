// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QColor>
#include <QPalette>

#include "app/ThemeManager.h"
#include "app/UiStyleTokens.h"
#include "theme/AnDesignTokens.h"

namespace
{

class ScopedThemePreferenceOverride final
{
public:
    ScopedThemePreferenceOverride() : m_originalPreference(an::ui::themePreference()) {}

    ~ScopedThemePreferenceOverride()
    {
        an::ui::setThemePreference(m_originalPreference);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemePreference preference) const
    {
        an::ui::setThemePreference(preference);
    }

private:
    an::ui::ThemePreference m_originalPreference;
};

class ScopedContrastProfileOverride final
{
public:
    ScopedContrastProfileOverride() : m_originalProfile(an::ui::themeContrastProfile()) {}

    ~ScopedContrastProfileOverride()
    {
        an::ui::setThemeContrastProfile(m_originalProfile);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemeContrastProfile profile) const
    {
        an::ui::setThemeContrastProfile(profile);
    }

private:
    an::ui::ThemeContrastProfile m_originalProfile;
};

} // anonymous namespace

TEST(ThemeSystemCascade, IconPaletteMatchesTheme)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    const auto [darkNormal, darkDisabled] = an::ui::iconPaletteForTheme(true);
    const auto [lightNormal, lightDisabled] = an::ui::iconPaletteForTheme(false);

    EXPECT_EQ(darkNormal, an::ui::resolvedDarkTokens().colors.textPrimary);
    EXPECT_EQ(darkDisabled, an::ui::resolvedDarkTokens().colors.textTertiary);
    EXPECT_EQ(lightNormal, an::ui::resolvedLightTokens().colors.textPrimary);
    EXPECT_EQ(lightDisabled, an::ui::resolvedLightTokens().colors.textTertiary);
}

TEST(ThemeSystemCascade, CascadeColorsFollowResolvedContrastProfile)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const QColor highSemanticAccent = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                     an::ui::styling::SemanticColorRole::AccentPrimary);
    const auto [highIconNormal, highIconDisabled] = an::ui::iconPaletteForTheme(true);
    const QColor highResolvedAccent = an::ui::resolvedDarkTokens().colors.accentPrimary;
    const QColor highResolvedIconNormal = an::ui::resolvedDarkTokens().colors.textPrimary;
    const QColor highResolvedIconDisabled = an::ui::resolvedDarkTokens().colors.textTertiary;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    const QColor softSemanticAccent = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                                     an::ui::styling::SemanticColorRole::AccentPrimary);
    const auto [softIconNormal, softIconDisabled] = an::ui::iconPaletteForTheme(true);
    const QColor softResolvedAccent = an::ui::resolvedDarkTokens().colors.accentPrimary;
    const QColor softResolvedIconNormal = an::ui::resolvedDarkTokens().colors.textPrimary;
    const QColor softResolvedIconDisabled = an::ui::resolvedDarkTokens().colors.textTertiary;

    EXPECT_EQ(highSemanticAccent, highResolvedAccent);
    EXPECT_EQ(softSemanticAccent, softResolvedAccent);
    EXPECT_EQ(highIconNormal, highResolvedIconNormal);
    EXPECT_EQ(softIconNormal, softResolvedIconNormal);
    EXPECT_EQ(highIconDisabled, highResolvedIconDisabled);
    EXPECT_EQ(softIconDisabled, softResolvedIconDisabled);

    EXPECT_NE(highSemanticAccent, softSemanticAccent);
    EXPECT_NE(highIconNormal, softIconNormal);
    EXPECT_NE(highIconDisabled, softIconDisabled);
}

TEST(ThemeSystemCascade, SemanticColorsRemainDistinctBetweenVariants)
{
    const QColor darkAccent = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                             an::ui::styling::SemanticColorRole::AccentPrimary);
    const QColor lightAccent = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Light,
                                                              an::ui::styling::SemanticColorRole::AccentPrimary);

    EXPECT_NE(darkAccent, lightAccent);
}

TEST(ThemeSystemCascade, SemanticColorByPaletteUsesDetectedVariant)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::High);

    QPalette dark;
    dark.setColor(QPalette::Window, QColor(10, 10, 10));

    const QColor color = an::ui::styling::semanticColor(dark, an::ui::styling::SemanticColorRole::SurfaceWindow);

    EXPECT_EQ(color, an::ui::resolvedDarkTokens().layers.surface0);
}

TEST(ThemeSystemCascade, SemanticSurfaceRolesFollowLayerTokens)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    const QColor darkWindow = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                             an::ui::styling::SemanticColorRole::SurfaceWindow);
    const QColor darkElevated = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                               an::ui::styling::SemanticColorRole::SurfaceElevated);
    const QColor darkOverlay = an::ui::styling::semanticColor(an::ui::styling::ThemeVariant::Dark,
                                                              an::ui::styling::SemanticColorRole::SurfaceOverlay);

    EXPECT_EQ(darkWindow, an::ui::resolvedDarkTokens().layers.surface0);
    EXPECT_EQ(darkElevated, an::ui::resolvedDarkTokens().layers.surface3);
    EXPECT_EQ(darkOverlay, an::ui::resolvedDarkTokens().layers.surface2);
}
