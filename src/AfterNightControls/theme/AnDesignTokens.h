// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFont>

#include <string>

namespace an::ui
{

enum class ThemePreference
{
    System,
    Light,
    Dark
};

enum class MaterialBackdropMode
{
    Auto,
    ForceFallback,
    PreferNative
};

enum class ThemeContrastProfile
{
    High,
    Balanced,
    Soft,
    Oled
};

enum class ThemeAccentFamily
{
    Blue,
    Teal,
    Amber,
    Amethyst,
    Rose
};

enum class ThemeSurfaceTintFamily
{
    Neutral,
    Ocean,
    Mint,
    Amethyst,
    Rose,
    Amber
};

enum class ThemeVariant
{
    Dark,
    Light
};

enum class SemanticColorRole
{
    SurfaceWindow,
    SurfaceElevated,
    SurfaceOverlay,
    SurfaceBorder,

    TextPrimary,
    TextSecondary,
    TextMuted,
    TextOnAccent,

    AccentPrimary,
    AccentHover,
    AccentSoft,

    HighlightBackground,
    HighlightBorder,
    CodeBackground,

    WarningBackground,
    WarningBorder,
    WarningText,
    DangerBackground,
    DangerBorder,
    DangerText,
    SuccessBackground,
    SuccessBorder,
    SuccessText,
    InfoBackground,
    InfoBorder,
    InfoText,

    StatusSuccess,
    StatusWarning,
    StatusError,
    StatusPending,
    StatusActive,
    StatusMaster
};

struct ColorTokens
{
    struct BackgroundGroup
    {
        QColor& primary;
        QColor& surface;
        QColor& card;
        QColor& cardPrimary;
        QColor& cardSecondary;
        QColor& input;
        QColor& hover;
    };

    struct ConstBackgroundGroup
    {
        const QColor& primary;
        const QColor& surface;
        const QColor& card;
        const QColor& cardPrimary;
        const QColor& cardSecondary;
        const QColor& input;
        const QColor& hover;
    };

    struct TextGroup
    {
        QColor& primary;
        QColor& secondary;
        QColor& tertiary;
        QColor& accent;
        QColor& onAccent;
    };

    struct ConstTextGroup
    {
        const QColor& primary;
        const QColor& secondary;
        const QColor& tertiary;
        const QColor& accent;
        const QColor& onAccent;
    };

    struct BorderGroup
    {
        QColor& primary;
        QColor& subtle;
        QColor& focus;
    };

    struct ConstBorderGroup
    {
        const QColor& primary;
        const QColor& subtle;
        const QColor& focus;
    };

    struct StatusGroup
    {
        QColor& success;
        QColor& warning;
        QColor& error;
        QColor& info;
        QColor& successSoft;
        QColor& warningSoft;
        QColor& errorSoft;
        QColor& infoSoft;
    };

    struct ConstStatusGroup
    {
        const QColor& success;
        const QColor& warning;
        const QColor& error;
        const QColor& info;
        const QColor& successSoft;
        const QColor& warningSoft;
        const QColor& errorSoft;
        const QColor& infoSoft;
    };

    struct AccentGroup
    {
        QColor& primary;
        QColor& hover;
        QColor& pressed;
        QColor& disabled;
        QColor& pinned;
        QColor& surfaceTint;
        QColor& surfaceBorderTint;
    };

    struct ConstAccentGroup
    {
        const QColor& primary;
        const QColor& hover;
        const QColor& pressed;
        const QColor& disabled;
        const QColor& pinned;
        const QColor& surfaceTint;
        const QColor& surfaceBorderTint;
    };

    struct EffectsGroup
    {
        QColor& shadowOverlay;
    };

    struct ConstEffectsGroup
    {
        const QColor& shadowOverlay;
    };

    QColor bgPrimary;
    QColor bgSurface;
    QColor bgCard;
    QColor bgCardPrimary;
    QColor bgCardSecondary;
    QColor bgInput;
    QColor bgHover;

    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor textAccent;
    QColor textOnAccent;

    QColor borderPrimary;
    QColor borderSubtle;
    QColor borderFocus;

    QColor statusSuccess;
    QColor statusWarning;
    QColor statusError;
    QColor statusInfo;
    QColor statusSuccessSoft;
    QColor statusWarningSoft;
    QColor statusErrorSoft;
    QColor statusInfoSoft;

    QColor accentPrimary;
    QColor accentHover;
    QColor accentPressed;
    QColor accentDisabled;
    QColor accentPinned;
    QColor cardSurfaceTint;
    QColor cardSurfaceBorderTint;

    QColor shadowOverlay;

    [[nodiscard]] BackgroundGroup bg()
    {
        return {bgPrimary, bgSurface, bgCard, bgCardPrimary, bgCardSecondary, bgInput, bgHover};
    }

    [[nodiscard]] ConstBackgroundGroup bg() const
    {
        return {bgPrimary, bgSurface, bgCard, bgCardPrimary, bgCardSecondary, bgInput, bgHover};
    }

    [[nodiscard]] TextGroup text()
    {
        return {textPrimary, textSecondary, textTertiary, textAccent, textOnAccent};
    }

    [[nodiscard]] ConstTextGroup text() const
    {
        return {textPrimary, textSecondary, textTertiary, textAccent, textOnAccent};
    }

    [[nodiscard]] BorderGroup border()
    {
        return {borderPrimary, borderSubtle, borderFocus};
    }

    [[nodiscard]] ConstBorderGroup border() const
    {
        return {borderPrimary, borderSubtle, borderFocus};
    }

    [[nodiscard]] StatusGroup status()
    {
        return {statusSuccess,     statusWarning,     statusError,     statusInfo,
                statusSuccessSoft, statusWarningSoft, statusErrorSoft, statusInfoSoft};
    }

    [[nodiscard]] ConstStatusGroup status() const
    {
        return {statusSuccess,     statusWarning,     statusError,     statusInfo,
                statusSuccessSoft, statusWarningSoft, statusErrorSoft, statusInfoSoft};
    }

    [[nodiscard]] AccentGroup accent()
    {
        return {accentPrimary, accentHover,     accentPressed,        accentDisabled,
                accentPinned,  cardSurfaceTint, cardSurfaceBorderTint};
    }

    [[nodiscard]] ConstAccentGroup accent() const
    {
        return {accentPrimary, accentHover,     accentPressed,        accentDisabled,
                accentPinned,  cardSurfaceTint, cardSurfaceBorderTint};
    }

    [[nodiscard]] EffectsGroup effects()
    {
        return {shadowOverlay};
    }

    [[nodiscard]] ConstEffectsGroup effects() const
    {
        return {shadowOverlay};
    }

    [[nodiscard]] bool operator==(const ColorTokens&) const = default;
};

struct SpacingTokens
{
    int xs = 4;
    int sm = 8;
    int md = 12;
    int lg = 16;
    int xl = 24;

    [[nodiscard]] bool operator==(const SpacingTokens&) const = default;
};

struct RadiusTokens
{
    int none = 0;
    int sm = 4;
    int md = 8;
    int lg = 12;
    int xl = 14;

    [[nodiscard]] bool operator==(const RadiusTokens&) const = default;
};

struct LayerTokens
{
    QColor surface0;
    QColor surface1;
    QColor surface2;
    QColor surface3;
    QColor surface4;

    [[nodiscard]] bool operator==(const LayerTokens&) const = default;
};

struct MaterialTokens
{
    // Deterministic material fallbacks used when native backdrop effects
    // (blur/acrylic/mica) are unavailable on the current platform.
    QColor solidSurface;
    QColor micaSurface;
    QColor acrylicSurface;

    [[nodiscard]] bool operator==(const MaterialTokens&) const = default;
};

struct TypographyTokens
{
    QFont heading1;
    QFont heading2;
    QFont heading3;
    QFont body;
    QFont caption;
    QFont monospace;

    [[nodiscard]] bool operator==(const TypographyTokens&) const = default;
};

struct DesignTokens
{
    ColorTokens colors;
    SpacingTokens spacing;
    RadiusTokens radius;
    LayerTokens layers;
    MaterialTokens materials;
    TypographyTokens typography;

    [[nodiscard]] bool operator==(const DesignTokens&) const = default;
};

[[nodiscard]] AFTERNIGHTCONTROLS_API const DesignTokens& tokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API const DesignTokens& darkTokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API const DesignTokens& lightTokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API const DesignTokens& resolvedDarkTokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API const DesignTokens& resolvedLightTokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor compositeOver(const QColor& foreground, const QColor& background);
[[nodiscard]] AFTERNIGHTCONTROLS_API double contrastRatio(const QColor& a, const QColor& b);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor bestContrastTextColor(const QColor& background, const QColor& preferred,
                                                                  const QColor& fallbackA, const QColor& fallbackB,
                                                                  double minimumRatio = 3.0);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor semanticColor(ThemeVariant variant, SemanticColorRole role);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor semanticColor(bool darkTheme, SemanticColorRole role);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor semanticColor(SemanticColorRole role);

AFTERNIGHTCONTROLS_API void refreshTokens();
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string dumpResolvedTokensReport();
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string dumpResolvedTokenDiffReport();
AFTERNIGHTCONTROLS_API void logResolvedTokensReport();
AFTERNIGHTCONTROLS_API void setThemePreference(ThemePreference preference);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemePreference themePreference();

AFTERNIGHTCONTROLS_API void setMaterialBackdropMode(MaterialBackdropMode mode);
[[nodiscard]] AFTERNIGHTCONTROLS_API MaterialBackdropMode materialBackdropMode();
AFTERNIGHTCONTROLS_API void setThemeContrastProfile(ThemeContrastProfile profile);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemeContrastProfile themeContrastProfile();
AFTERNIGHTCONTROLS_API void setThemeAccentFamily(ThemeAccentFamily family);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemeAccentFamily themeAccentFamily();
AFTERNIGHTCONTROLS_API void setThemeSurfaceTintFamily(ThemeSurfaceTintFamily family);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemeSurfaceTintFamily themeSurfaceTintFamily();
[[nodiscard]] AFTERNIGHTCONTROLS_API bool nativeBackdropSupported();
[[nodiscard]] AFTERNIGHTCONTROLS_API bool usingNativeBackdropMaterials();

[[nodiscard]] AFTERNIGHTCONTROLS_API bool isDarkTheme();

} // namespace an::ui
