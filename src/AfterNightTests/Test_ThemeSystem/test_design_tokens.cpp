// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QWidget>

#include <array>
#include <cmath>

#include "theme/AnDesignTokens.h"

namespace
{

class ScopedPaletteOverride final
{
public:
    ScopedPaletteOverride()
    {
        if (QGuiApplication::instance())
        {
            m_originalPalette = QGuiApplication::palette();
        }
    }

    ~ScopedPaletteOverride()
    {
        if (QGuiApplication::instance())
        {
            QGuiApplication::setPalette(m_originalPalette);
            QCoreApplication::processEvents();
            an::ui::refreshTokens();
        }
    }

    void setWindowColor(const QColor& color) const
    {
        if (!QGuiApplication::instance())
        {
            return;
        }

        QPalette palette = QGuiApplication::palette();
        palette.setColor(QPalette::Window, color);
        QGuiApplication::setPalette(palette);
        QCoreApplication::processEvents();
        an::ui::refreshTokens();
    }

private:
    QPalette m_originalPalette;
};

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

class ScopedMaterialBackdropModeOverride final
{
public:
    ScopedMaterialBackdropModeOverride() : m_originalMode(an::ui::materialBackdropMode()) {}

    ~ScopedMaterialBackdropModeOverride()
    {
        an::ui::setMaterialBackdropMode(m_originalMode);
        an::ui::refreshTokens();
    }

    void set(const an::ui::MaterialBackdropMode mode) const
    {
        an::ui::setMaterialBackdropMode(mode);
    }

private:
    an::ui::MaterialBackdropMode m_originalMode;
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

class ScopedAccentFamilyOverride final
{
public:
    ScopedAccentFamilyOverride() : m_originalFamily(an::ui::themeAccentFamily()) {}

    ~ScopedAccentFamilyOverride()
    {
        an::ui::setThemeAccentFamily(m_originalFamily);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemeAccentFamily family) const
    {
        an::ui::setThemeAccentFamily(family);
    }

private:
    an::ui::ThemeAccentFamily m_originalFamily;
};

class ScopedSurfaceTintFamilyOverride final
{
public:
    ScopedSurfaceTintFamilyOverride() : m_originalFamily(an::ui::themeSurfaceTintFamily()) {}

    ~ScopedSurfaceTintFamilyOverride()
    {
        an::ui::setThemeSurfaceTintFamily(m_originalFamily);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemeSurfaceTintFamily family) const
    {
        an::ui::setThemeSurfaceTintFamily(family);
    }

private:
    an::ui::ThemeSurfaceTintFamily m_originalFamily;
};

std::array<QColor, 22> colorsFrom(const an::ui::ColorTokens& colors)
{
    return {
        colors.bgPrimary,     colors.bgSurface,     colors.bgCard,        colors.bgCardPrimary, colors.bgCardSecondary,
        colors.bgInput,       colors.bgHover,       colors.textPrimary,   colors.textSecondary, colors.textTertiary,
        colors.textAccent,    colors.textOnAccent,  colors.borderPrimary, colors.borderSubtle,  colors.borderFocus,
        colors.statusSuccess, colors.statusWarning, colors.statusError,   colors.statusInfo,    colors.accentPrimary,
        colors.accentPinned,  colors.shadowOverlay,
    };
}

double linearizedSrgbChannel(const int channel)
{
    const double value = static_cast<double>(channel) / 255.0;
    if (value <= 0.04045)
    {
        return value / 12.92;
    }

    return std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    const double red = linearizedSrgbChannel(color.red());
    const double green = linearizedSrgbChannel(color.green());
    const double blue = linearizedSrgbChannel(color.blue());
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double luminanceA = relativeLuminance(a);
    const double luminanceB = relativeLuminance(b);
    const double lighter = (std::max)(luminanceA, luminanceB);
    const double darker = (std::min)(luminanceA, luminanceB);
    return (lighter + 0.05) / (darker + 0.05);
}

} // anonymous namespace

TEST(ThemeSystemDesignTokens, DesignTokens_DarkThemeHasCorrectBgPrimary)
{
    EXPECT_EQ(an::ui::darkTokens().colors.bgPrimary, QColor(14, 14, 17, 255));
}

TEST(ThemeSystemDesignTokens, DesignTokens_DarkThemeTextContrastIsBalanced)
{
    const auto& colors = an::ui::darkTokens().colors;
    const double ratio = contrastRatio(colors.textPrimary, colors.bgPrimary);
    EXPECT_GE(ratio, 9.5);
    EXPECT_LE(ratio, 18.5);
}

TEST(ThemeSystemDesignTokens, DesignTokens_LightThemeHasCorrectTextPrimary)
{
    EXPECT_EQ(an::ui::lightTokens().colors.textPrimary, QColor(24, 24, 27, 255));
}

TEST(ThemeSystemDesignTokens, DesignTokens_LayerSurfacesMapToCoreColorRoles)
{
    const auto& dark = an::ui::darkTokens();
    EXPECT_EQ(dark.layers.surface0, dark.colors.bgPrimary);
    EXPECT_EQ(dark.layers.surface1, dark.colors.bgSurface);
    EXPECT_EQ(dark.layers.surface2, dark.colors.bgCardSecondary);
    EXPECT_EQ(dark.layers.surface3, dark.colors.bgCardPrimary);
    EXPECT_EQ(dark.layers.surface4, dark.colors.bgHover);
}

TEST(ThemeSystemDesignTokens, DesignTokens_MaterialFallbackSurfacesAreDeterministic)
{
    const auto& dark = an::ui::darkTokens();
    const auto& light = an::ui::lightTokens();

    EXPECT_EQ(dark.materials.micaSurface, dark.colors.bgPrimary);
    EXPECT_EQ(dark.materials.solidSurface, dark.colors.bgSurface);
    EXPECT_EQ(dark.materials.acrylicSurface, dark.colors.bgCard);

    EXPECT_EQ(light.materials.micaSurface, light.colors.bgPrimary);
    EXPECT_EQ(light.materials.solidSurface, light.colors.bgSurface);
    EXPECT_EQ(light.materials.acrylicSurface, light.colors.bgCard);
}

TEST(ThemeSystemDesignTokens, DesignTokens_ForceFallbackDisablesNativeBackdropMaterials)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedMaterialBackdropModeOverride backdropModeGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    backdropModeGuard.set(an::ui::MaterialBackdropMode::ForceFallback);
    an::ui::refreshTokens();

    EXPECT_FALSE(an::ui::usingNativeBackdropMaterials());
    EXPECT_EQ(an::ui::tokens().materials.micaSurface, an::ui::resolvedDarkTokens().materials.micaSurface);
    EXPECT_EQ(an::ui::tokens().materials.acrylicSurface, an::ui::resolvedDarkTokens().materials.acrylicSurface);
}

// Not working on Windows... needs more work
TEST(ThemeSystemDesignTokens, DISABLED_DesignTokens_PreferNativeUsesNativeWhenSupported)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedMaterialBackdropModeOverride backdropModeGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);

    backdropModeGuard.set(an::ui::MaterialBackdropMode::ForceFallback);
    an::ui::refreshTokens();
    const auto fallbackDark = an::ui::resolvedDarkTokens();

    backdropModeGuard.set(an::ui::MaterialBackdropMode::PreferNative);
    an::ui::refreshTokens();

    const auto& resolvedDark = an::ui::resolvedDarkTokens();

    if (an::ui::nativeBackdropSupported())
    {
        EXPECT_TRUE(an::ui::usingNativeBackdropMaterials());
        EXPECT_NE(resolvedDark.materials.micaSurface, fallbackDark.materials.micaSurface);
        EXPECT_NE(resolvedDark.materials.acrylicSurface, fallbackDark.materials.acrylicSurface);
    }
    else
    {
        EXPECT_FALSE(an::ui::usingNativeBackdropMaterials());
        EXPECT_EQ(resolvedDark.materials.micaSurface, fallbackDark.materials.micaSurface);
        EXPECT_EQ(resolvedDark.materials.acrylicSurface, fallbackDark.materials.acrylicSurface);
    }
}

TEST(ThemeSystemDesignTokens, DesignTokens_RefreshDetectsThemeChange)
{
    ScopedThemePreferenceOverride preferenceGuard;
    preferenceGuard.set(an::ui::ThemePreference::System);

    ScopedPaletteOverride paletteGuard;
    paletteGuard.setWindowColor(QColor(10, 10, 10));
    EXPECT_TRUE(an::ui::isDarkTheme());

    paletteGuard.setWindowColor(QColor(245, 245, 245));
    EXPECT_FALSE(an::ui::isDarkTheme());
    EXPECT_EQ(an::ui::tokens().colors.bgPrimary, an::ui::lightTokens().colors.bgPrimary);
}

// Every component refreshes the tokens when it refreshes its theme, and a published token set is kept for the life of
// the process so that a reference tokens() handed out stays valid. A refresh that resolves the set already active must
// therefore publish nothing, or the kept sets grow with every widget a session builds.
TEST(ThemeSystemDesignTokens, DesignTokens_RefreshThatResolvesTheActiveTokensPublishesNothing)
{
    ScopedThemePreferenceOverride preferenceGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);
    const an::ui::DesignTokens* active = &an::ui::tokens();

    for (int refresh = 0; refresh < 8; ++refresh)
    {
        an::ui::refreshTokens();
    }

    EXPECT_EQ(&an::ui::tokens(), active);
}

TEST(ThemeSystemDesignTokens, DesignTokens_ChangedTokensArePublishedAndAnEarlierSetStaysValid)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    preferenceGuard.set(an::ui::ThemePreference::Dark);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const an::ui::DesignTokens& high = an::ui::tokens();
    const QColor highBackground = high.colors.bgPrimary;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    EXPECT_NE(&an::ui::tokens(), &high);
    EXPECT_NE(an::ui::tokens().colors.bgPrimary, highBackground);
    EXPECT_EQ(high.colors.bgPrimary, highBackground);
}

TEST(ThemeSystemDesignTokens, DesignTokens_AllColorsHaveNonZeroAlpha)
{
    for (const QColor& color : colorsFrom(an::ui::darkTokens().colors))
    {
        EXPECT_GT(color.alpha(), 0);
    }

    for (const QColor& color : colorsFrom(an::ui::lightTokens().colors))
    {
        EXPECT_GT(color.alpha(), 0);
    }
}

TEST(ThemeSystemDesignTokens, DesignTokens_DarkAndLightDiffer)
{
    EXPECT_NE(an::ui::darkTokens().colors.bgPrimary, an::ui::lightTokens().colors.bgPrimary);
    EXPECT_NE(an::ui::darkTokens().colors.textPrimary, an::ui::lightTokens().colors.textPrimary);
    EXPECT_NE(an::ui::darkTokens().colors.accentPrimary, an::ui::lightTokens().colors.accentPrimary);
}

TEST(ThemeSystemDesignTokens, DesignTokens_ContrastProfilesShiftDarkThemeIntensity)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const QColor highBg = an::ui::tokens().colors.bgPrimary;
    const QColor highAccent = an::ui::tokens().colors.accentPrimary;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    const QColor softBg = an::ui::tokens().colors.bgPrimary;
    const QColor softAccent = an::ui::tokens().colors.accentPrimary;

    EXPECT_LT(highBg.value(), softBg.value());
    EXPECT_GT(highAccent.saturation(), softAccent.saturation());
}

TEST(ThemeSystemDesignTokens, DesignTokens_OledProfileProducesPureBlackBackground)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Oled);

    const auto& c = an::ui::tokens().colors;

    // OLED surfaces must be pure black for AMOLED pixel-off.
    EXPECT_EQ(c.bgPrimary, QColor(0, 0, 0, 255));
    EXPECT_EQ(c.bgSurface, QColor(0, 0, 0, 255));

    // Card surfaces should be near-black but not identical to the background.
    EXPECT_LE(c.bgCardPrimary.value(), 15);
    EXPECT_NE(c.bgCardPrimary, c.bgPrimary);
}

TEST(ThemeSystemDesignTokens, DesignTokens_OledProfileBoostsAllStatusColors)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);

    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);
    const QColor balancedSuccess = an::ui::tokens().colors.statusSuccess;
    const QColor balancedWarning = an::ui::tokens().colors.statusWarning;
    const QColor balancedError = an::ui::tokens().colors.statusError;
    const QColor balancedInfo = an::ui::tokens().colors.statusInfo;

    profileGuard.set(an::ui::ThemeContrastProfile::Oled);
    const QColor oledSuccess = an::ui::tokens().colors.statusSuccess;
    const QColor oledWarning = an::ui::tokens().colors.statusWarning;
    const QColor oledError = an::ui::tokens().colors.statusError;
    const QColor oledInfo = an::ui::tokens().colors.statusInfo;

    // All status colors should be boosted (different from balanced).
    EXPECT_NE(oledSuccess, balancedSuccess);
    EXPECT_NE(oledWarning, balancedWarning);
    EXPECT_NE(oledError, balancedError);
    EXPECT_NE(oledInfo, balancedInfo);
}

TEST(ThemeSystemDesignTokens, DesignTokens_OledProfileSoftTokensTrackBaseColors)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Oled);

    const auto& c = an::ui::tokens().colors;

    // Soft variants must share the RGB of the corresponding base status color
    // (only the alpha channel differs).
    EXPECT_EQ(c.statusSuccessSoft.red(), c.statusSuccess.red());
    EXPECT_EQ(c.statusSuccessSoft.green(), c.statusSuccess.green());
    EXPECT_EQ(c.statusSuccessSoft.blue(), c.statusSuccess.blue());
    EXPECT_LT(c.statusSuccessSoft.alpha(), c.statusSuccess.alpha());

    EXPECT_EQ(c.statusInfoSoft.red(), c.statusInfo.red());
    EXPECT_EQ(c.statusInfoSoft.green(), c.statusInfo.green());
    EXPECT_EQ(c.statusInfoSoft.blue(), c.statusInfo.blue());
    EXPECT_LT(c.statusInfoSoft.alpha(), c.statusInfo.alpha());
}

TEST(ThemeSystemDesignTokens, DesignTokens_OledLightModeFallsBackToBalanced)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;

    preferenceGuard.set(an::ui::ThemePreference::Light);

    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);
    const QColor balancedBg = an::ui::tokens().colors.bgPrimary;

    profileGuard.set(an::ui::ThemeContrastProfile::Oled);
    const QColor oledBg = an::ui::tokens().colors.bgPrimary;

    // In light mode, OLED should silently fall back to Balanced.
    EXPECT_EQ(oledBg, balancedBg);
}

TEST(ThemeSystemDesignTokens, DesignTokens_AccentFamiliesRetintAccentRoles)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Blue);
    const QColor blueAccent = an::ui::tokens().colors.accentPrimary;
    const QColor blueFocus = an::ui::tokens().colors.borderFocus;
    const QColor blueTextAccent = an::ui::tokens().colors.textAccent;

    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Teal);
    const QColor tealAccent = an::ui::tokens().colors.accentPrimary;
    const QColor tealFocus = an::ui::tokens().colors.borderFocus;
    const QColor tealTextAccent = an::ui::tokens().colors.textAccent;

    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Amber);
    const QColor amberAccent = an::ui::tokens().colors.accentPrimary;
    const QColor amberFocus = an::ui::tokens().colors.borderFocus;
    const QColor amberTextAccent = an::ui::tokens().colors.textAccent;

    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Amethyst);
    const QColor amethystAccent = an::ui::tokens().colors.accentPrimary;
    const QColor amethystFocus = an::ui::tokens().colors.borderFocus;
    const QColor amethystTextAccent = an::ui::tokens().colors.textAccent;

    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Rose);
    const QColor roseAccent = an::ui::tokens().colors.accentPrimary;
    const QColor roseFocus = an::ui::tokens().colors.borderFocus;
    const QColor roseTextAccent = an::ui::tokens().colors.textAccent;

    EXPECT_NE(blueAccent, tealAccent);
    EXPECT_NE(blueAccent, amberAccent);
    EXPECT_NE(tealAccent, amberAccent);
    EXPECT_NE(amberAccent, amethystAccent);
    EXPECT_NE(amethystAccent, roseAccent);

    EXPECT_NE(blueFocus, tealFocus);
    EXPECT_NE(blueFocus, amberFocus);
    EXPECT_NE(tealFocus, amberFocus);
    EXPECT_NE(amberFocus, amethystFocus);
    EXPECT_NE(amethystFocus, roseFocus);

    EXPECT_NE(blueTextAccent, tealTextAccent);
    EXPECT_NE(blueTextAccent, amberTextAccent);
    EXPECT_NE(tealTextAccent, amberTextAccent);
    EXPECT_NE(amberTextAccent, amethystTextAccent);
    EXPECT_NE(amethystTextAccent, roseTextAccent);
}

TEST(ThemeSystemDesignTokens, DesignTokens_ContrastProfilesRemainActiveWithAccentFamily)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Teal);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const QColor highAccent = an::ui::tokens().colors.accentPrimary;
    const QColor highBg = an::ui::tokens().colors.bgPrimary;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    const QColor softAccent = an::ui::tokens().colors.accentPrimary;
    const QColor softBg = an::ui::tokens().colors.bgPrimary;

    EXPECT_NE(highAccent, softAccent);
    EXPECT_LT(highBg.value(), softBg.value());
}

TEST(ThemeSystemDesignTokens, DesignTokens_SurfaceTintFamiliesRetintCardSurfaces)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedSurfaceTintFamilyOverride surfaceTintGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Neutral);
    const QColor neutralCard = an::ui::tokens().colors.bgCard;
    const QColor neutralCardSecondary = an::ui::tokens().colors.bgCardSecondary;
    const QColor neutralTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor neutralTintBorder = an::ui::tokens().colors.cardSurfaceBorderTint;

    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Ocean);
    const QColor blueCard = an::ui::tokens().colors.bgCard;
    const QColor blueCardSecondary = an::ui::tokens().colors.bgCardSecondary;
    const QColor blueTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor blueTintBorder = an::ui::tokens().colors.cardSurfaceBorderTint;

    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Mint);
    const QColor tealCard = an::ui::tokens().colors.bgCard;
    const QColor tealCardSecondary = an::ui::tokens().colors.bgCardSecondary;
    const QColor tealTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor tealTintBorder = an::ui::tokens().colors.cardSurfaceBorderTint;

    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Amethyst);
    const QColor violetCard = an::ui::tokens().colors.bgCard;
    const QColor violetCardSecondary = an::ui::tokens().colors.bgCardSecondary;
    const QColor violetTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor violetTintBorder = an::ui::tokens().colors.cardSurfaceBorderTint;

    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Amber);
    const QColor indigoCard = an::ui::tokens().colors.bgCard;
    const QColor indigoCardSecondary = an::ui::tokens().colors.bgCardSecondary;
    const QColor indigoTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor indigoTintBorder = an::ui::tokens().colors.cardSurfaceBorderTint;

    EXPECT_FALSE(neutralTint.isValid());
    EXPECT_FALSE(neutralTintBorder.isValid());

    EXPECT_TRUE(blueTint.isValid());
    EXPECT_TRUE(tealTint.isValid());
    EXPECT_TRUE(violetTint.isValid());
    EXPECT_TRUE(indigoTint.isValid());
    EXPECT_TRUE(blueTintBorder.isValid());
    EXPECT_TRUE(tealTintBorder.isValid());
    EXPECT_TRUE(violetTintBorder.isValid());
    EXPECT_TRUE(indigoTintBorder.isValid());

    EXPECT_NE(blueTint, tealTint);
    EXPECT_NE(tealTint, violetTint);
    EXPECT_NE(violetTint, indigoTint);
    EXPECT_NE(blueTint, violetTint);
    EXPECT_NE(blueTint, indigoTint);
    EXPECT_NE(tealTint, indigoTint);
    EXPECT_NE(blueTintBorder, tealTintBorder);
    EXPECT_NE(blueTintBorder, violetTintBorder);
    EXPECT_NE(blueTintBorder, indigoTintBorder);
    EXPECT_NE(tealTintBorder, violetTintBorder);
    EXPECT_NE(violetTintBorder, indigoTintBorder);
    EXPECT_NE(tealTintBorder, indigoTintBorder);

    // Surface tint families should not recolor global card/background tokens.
    EXPECT_EQ(neutralCard, blueCard);
    EXPECT_EQ(neutralCard, tealCard);
    EXPECT_EQ(neutralCard, violetCard);
    EXPECT_EQ(neutralCard, indigoCard);
    EXPECT_EQ(neutralCardSecondary, blueCardSecondary);
    EXPECT_EQ(neutralCardSecondary, tealCardSecondary);
    EXPECT_EQ(neutralCardSecondary, violetCardSecondary);
    EXPECT_EQ(neutralCardSecondary, indigoCardSecondary);
}

TEST(ThemeSystemDesignTokens, DesignTokens_ContrastProfilesRemainActiveWithSurfaceTintFamily)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedSurfaceTintFamilyOverride surfaceTintGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Ocean);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const QColor highCard = an::ui::tokens().colors.bgCard;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    const QColor softCard = an::ui::tokens().colors.bgCard;

    EXPECT_LT(highCard.value(), softCard.value());
}

TEST(ThemeSystemDesignTokens, DesignTokens_SurfaceTintProfilesFollowIntensityOrder)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedSurfaceTintFamilyOverride surfaceTintGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    surfaceTintGuard.set(an::ui::ThemeSurfaceTintFamily::Ocean);

    profileGuard.set(an::ui::ThemeContrastProfile::High);
    const QColor highTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor highBorderTint = an::ui::tokens().colors.cardSurfaceBorderTint;

    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);
    const QColor balancedTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor balancedBorderTint = an::ui::tokens().colors.cardSurfaceBorderTint;

    profileGuard.set(an::ui::ThemeContrastProfile::Soft);
    const QColor softTint = an::ui::tokens().colors.cardSurfaceTint;
    const QColor softBorderTint = an::ui::tokens().colors.cardSurfaceBorderTint;

    ASSERT_TRUE(highTint.isValid());
    ASSERT_TRUE(balancedTint.isValid());
    ASSERT_TRUE(softTint.isValid());
    ASSERT_TRUE(highBorderTint.isValid());
    ASSERT_TRUE(balancedBorderTint.isValid());
    ASSERT_TRUE(softBorderTint.isValid());

    EXPECT_GT(highTint.saturation(), balancedTint.saturation());
    EXPECT_GT(balancedTint.saturation(), softTint.saturation());
    EXPECT_GT(highTint.value(), balancedTint.value());
    EXPECT_GT(balancedTint.value(), softTint.value());

    EXPECT_GT(highBorderTint.saturation(), balancedBorderTint.saturation());
    EXPECT_GT(balancedBorderTint.saturation(), softBorderTint.saturation());
    EXPECT_GT(highBorderTint.value(), balancedBorderTint.value());
    EXPECT_GT(balancedBorderTint.value(), softBorderTint.value());
}

TEST(ThemeSystemDesignTokens, DesignTokens_ExplicitPreferenceOverridesPalette)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedPaletteOverride paletteGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    paletteGuard.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    EXPECT_TRUE(an::ui::isDarkTheme());
    EXPECT_EQ(an::ui::tokens().colors.bgPrimary, an::ui::resolvedDarkTokens().colors.bgPrimary);

    preferenceGuard.set(an::ui::ThemePreference::Light);
    paletteGuard.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    EXPECT_FALSE(an::ui::isDarkTheme());
    EXPECT_EQ(an::ui::tokens().colors.bgPrimary, an::ui::resolvedLightTokens().colors.bgPrimary);
}

// ── Typography token tests ─────────────────────────────────────────────────

TEST(ThemeSystemTypographyTokens, Typography_HeadingSizesAreRelativeToBody)
{
    const auto& typo = an::ui::darkTokens().typography;
    const int bodySize = typo.body.pointSize();

    ASSERT_GT(bodySize, 0) << "Body font must have a positive point size";
    EXPECT_EQ(typo.heading1.pointSize(), bodySize + 4);
    EXPECT_EQ(typo.heading2.pointSize(), bodySize + 2);
    EXPECT_EQ(typo.heading3.pointSize(), bodySize + 1);
}

TEST(ThemeSystemTypographyTokens, Typography_CaptionIsSmallerThanBody)
{
    const auto& typo = an::ui::darkTokens().typography;
    const int bodySize = typo.body.pointSize();

    ASSERT_GT(bodySize, 0);
    EXPECT_LT(typo.caption.pointSize(), bodySize);
}

TEST(ThemeSystemTypographyTokens, Typography_HeadingWeightsAreCorrect)
{
    const auto& typo = an::ui::darkTokens().typography;

    EXPECT_EQ(typo.heading1.weight(), QFont::Bold);
    EXPECT_EQ(typo.heading2.weight(), QFont::DemiBold);
    EXPECT_EQ(typo.heading3.weight(), QFont::DemiBold);
}

TEST(ThemeSystemTypographyTokens, Typography_MonospaceHasMonospaceHint)
{
    const auto& typo = an::ui::darkTokens().typography;
    EXPECT_EQ(typo.monospace.styleHint(), QFont::Monospace);
}

TEST(ThemeSystemTypographyTokens, Typography_DarkAndLightShareSameHierarchy)
{
    // Typography sizing is theme-independent (relative to system font).
    const auto& dark = an::ui::darkTokens().typography;
    const auto& light = an::ui::lightTokens().typography;

    EXPECT_EQ(dark.heading1.pointSize(), light.heading1.pointSize());
    EXPECT_EQ(dark.heading2.pointSize(), light.heading2.pointSize());
    EXPECT_EQ(dark.heading3.pointSize(), light.heading3.pointSize());
    EXPECT_EQ(dark.body.pointSize(), light.body.pointSize());
    EXPECT_EQ(dark.caption.pointSize(), light.caption.pointSize());
}

TEST(ThemeSystemTypographyTokens, Typography_HeadingSizeOrderIsMonotonic)
{
    const auto& typo = an::ui::darkTokens().typography;

    EXPECT_GT(typo.heading1.pointSize(), typo.heading2.pointSize());
    EXPECT_GT(typo.heading2.pointSize(), typo.heading3.pointSize());
    EXPECT_GT(typo.heading3.pointSize(), typo.body.pointSize());
    EXPECT_GT(typo.body.pointSize(), typo.caption.pointSize());
}

// ── Dynamic textOnAccent tests ─────────────────────────────────────────────

TEST(ThemeSystemDesignTokens, DesignTokens_TextOnAccentIsBlackForDarkBlueAccent)
{
    // Dark-mode Blue accent (#60A5FA, luminance ~0.37) is bright enough that
    // black text provides substantially higher contrast (~8.3:1) than white
    // (~2.5:1), so the dynamic algorithm correctly selects black.
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);
    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Blue);

    const QColor textOnAccent = an::ui::resolvedDarkTokens().colors.textOnAccent;
    EXPECT_EQ(textOnAccent, QColor(0, 0, 0)) << "Dark Blue accent (#60A5FA) is bright; black text has higher contrast";
}

TEST(ThemeSystemDesignTokens, DesignTokens_TextOnAccentIsWhiteForBlueAccentLight)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Light);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);
    accentFamilyGuard.set(an::ui::ThemeAccentFamily::Blue);

    const QColor textOnAccent = an::ui::resolvedLightTokens().colors.textOnAccent;
    EXPECT_EQ(textOnAccent, QColor(255, 255, 255)) << "Blue accent in light mode should have white text-on-accent";
}

TEST(ThemeSystemDesignTokens, DesignTokens_TextOnAccentMeetsContrastThresholdAllFamilies)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    const std::array<an::ui::ThemeAccentFamily, 5> families = {
        an::ui::ThemeAccentFamily::Blue,     an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
        an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose,
    };

    for (const auto family : families)
    {
        accentFamilyGuard.set(family);

        // Dark variant
        {
            const auto& darkTokens = an::ui::resolvedDarkTokens();
            const double ratio = contrastRatio(darkTokens.colors.textOnAccent, darkTokens.colors.accentPrimary);
            EXPECT_GE(ratio, 3.0) << "Dark textOnAccent must have >=3:1 contrast against accentPrimary "
                                  << "(family=" << static_cast<int>(family) << ", ratio=" << ratio << ")";
        }

        // Light variant
        {
            const auto& lightTokens = an::ui::resolvedLightTokens();
            const double ratio = contrastRatio(lightTokens.colors.textOnAccent, lightTokens.colors.accentPrimary);
            EXPECT_GE(ratio, 3.0) << "Light textOnAccent must have >=3:1 contrast against accentPrimary "
                                  << "(family=" << static_cast<int>(family) << ", ratio=" << ratio << ")";
        }
    }
}

TEST(ThemeSystemDesignTokens, DesignTokens_TextOnAccentMeetsContrastAllProfiles)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);

    const std::array<an::ui::ThemeContrastProfile, 3> profiles = {
        an::ui::ThemeContrastProfile::High,
        an::ui::ThemeContrastProfile::Balanced,
        an::ui::ThemeContrastProfile::Soft,
    };

    const std::array<an::ui::ThemeAccentFamily, 5> families = {
        an::ui::ThemeAccentFamily::Blue,     an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
        an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose,
    };

    for (const auto profile : profiles)
    {
        profileGuard.set(profile);

        for (const auto family : families)
        {
            accentFamilyGuard.set(family);

            const auto& darkTokens = an::ui::resolvedDarkTokens();
            const double ratio = contrastRatio(darkTokens.colors.textOnAccent, darkTokens.colors.accentPrimary);
            EXPECT_GE(ratio, 3.0) << "textOnAccent contrast too low (profile=" << static_cast<int>(profile)
                                  << ", family=" << static_cast<int>(family) << ", ratio=" << ratio << ")";
        }
    }
}

TEST(ThemeSystemDesignTokens, DesignTokens_TextOnAccentIsOnlyWhiteOrBlack)
{
    ScopedThemePreferenceOverride preferenceGuard;
    ScopedContrastProfileOverride profileGuard;
    ScopedAccentFamilyOverride accentFamilyGuard;

    preferenceGuard.set(an::ui::ThemePreference::Dark);
    profileGuard.set(an::ui::ThemeContrastProfile::Balanced);

    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);

    const std::array<an::ui::ThemeAccentFamily, 5> families = {
        an::ui::ThemeAccentFamily::Blue,     an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
        an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose,
    };

    for (const auto family : families)
    {
        accentFamilyGuard.set(family);

        const QColor darkOnAccent = an::ui::resolvedDarkTokens().colors.textOnAccent;
        EXPECT_TRUE(darkOnAccent == white || darkOnAccent == black)
            << "textOnAccent must be pure white or black, got " << darkOnAccent.name().toStdString()
            << " (family=" << static_cast<int>(family) << ")";

        const QColor lightOnAccent = an::ui::resolvedLightTokens().colors.textOnAccent;
        EXPECT_TRUE(lightOnAccent == white || lightOnAccent == black)
            << "textOnAccent must be pure white or black, got " << lightOnAccent.name().toStdString()
            << " (family=" << static_cast<int>(family) << ")";
    }
}
