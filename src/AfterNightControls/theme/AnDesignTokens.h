// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "theme/StyleProfile.h"

#include <QColor>
#include <QFont>

#include <optional>
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

/// How much of the surface tint goes into the surfaces and the lines under the Observatory profile: Off, Subtle 5 % and
/// 8 %, Balanced 8 % and 12 % (1.1's blend), Strong 12 % and 18 %. The Neutral tint is Off whatever the strength. The
/// Classic profile has one blend and ignores it.
enum class ThemeTintStrength
{
    Off,
    Subtle,
    Balanced,
    Strong
};

/// How dense the Observatory profile lays the interface out: Compact (the default) or Comfortable, a column of the
/// metric tokens each. The Classic profile has AfterNight 1.1's sizes and ignores it.
enum class ThemeDensity
{
    Compact,
    Comfortable
};

/// A colour preset: an accent, a surface tint and a tint strength chosen together under one name. The theme and the
/// contrast profile stay the user's. A combination no preset names is `Custom`, so a part changed by hand leaves the
/// preset; the preset in force is always the one the parts in force make.
enum class ThemePreset
{
    SlateBlue, ///< Blue, Neutral, Balanced: the default.
    Observatory, ///< Amber, Ocean, Strong.
    DeepSea, ///< Teal, Ocean, Balanced.
    Graphite, ///< Amber, Neutral, Balanced.
    Nebula, ///< Rose, Amethyst, Balanced.
    Aurora, ///< Teal, Mint, Subtle.
    Custom ///< Any other combination.
};

/// The three parts a preset sets.
struct ThemePresetParts
{
    ThemeAccentFamily accent = ThemeAccentFamily::Blue;
    ThemeSurfaceTintFamily tint = ThemeSurfaceTintFamily::Neutral;
    ThemeTintStrength strength = ThemeTintStrength::Balanced;

    [[nodiscard]] bool operator==(const ThemePresetParts&) const = default;
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

    // The roles the Observatory profile names. Under the Classic profile each holds the colour AfterNight 1.1 paints
    // in that role, so a widget shared by both profiles reads it under either.

    QColor bgCanvas; ///< Behind an image in a view.
    QColor bgWell; ///< Behind a plot: dark in both themes, as an image is judged against dark.
    QColor bgSnack; ///< A transient message's fill.
    QColor textOnSnack; ///< Text on bgSnack.
    QColor stateLayer; ///< Laid over a control at 8 % while it is hovered and at 12 % while it is pressed.
    QColor scrim; ///< Under text laid over an image.
    QColor scrimSoft; ///< A lighter scrim: under a control laid over an image.
    QColor edge; ///< The 1 px line under a raised part (a menu, a tooltip, a popover).
    QColor textOnImage; ///< Text laid over an image, on a scrim.
    QColor textOnImageSecondary; ///< Secondary text laid over an image.
    QColor accentOnImage; ///< The accent drawn over an image.
    QColor textOnAccentImage; ///< Text on an accentOnImage fill.
    QColor accentContainer; ///< A chosen item's tonal fill.
    QColor textOnAccentContainer; ///< Text and icons on accentContainer.
    QColor accentTrack; ///< A slider's or a progress's track.
    QColor accentSoft; ///< The accent at 16 %: a selected row.
    QColor accentSofter; ///< The accent at 8 %: a hovered item of an accent group.
    QColor accentLine; ///< The accent at 45 %: an accent outline.
    QColor channelRed; ///< A channel's dot or label.
    QColor channelGreen;
    QColor channelBlue;
    QColor channelGray;
    QColor plotRed; ///< A channel's curve in a well.
    QColor plotGreen;
    QColor plotBlue;
    QColor plotGray;

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

/// The sizes of the interface, in logical pixels: what a control, a row and a bar are high, the font sizes, a panel's
/// padding and gaps. Under the Observatory profile they are the density's column (the defaults below are Compact's);
/// under the Classic profile they are AfterNight 1.1's where 1.1 has a size for the role and Compact's where it has
/// none. The proxy style sizes the standard widgets from them, and a widget sizes itself from them in its sizeHint().
struct MetricTokens
{
    int fontBody = 13; ///< The body text.
    int fontSmall = 12; ///< Secondary text: a status line, a tab's name, a key and its value.
    int fontCaption = 11; ///< A caption, a chip's text, a table's header.
    int control = 32; ///< A button, a field, a combo box, a spin box, an icon button (a square).
    int controlSmall = 28; ///< A small button; a tab inside a panel.
    int controlXSmall = 24; ///< An icon button in a strip.
    int controlLarge = 40; ///< A large button.
    int rowTable = 30; ///< A row of a table, a list or a tree, and a table's header.
    int rowDense = 28; ///< A menu's item; a row of a dense list.
    int rowKeyValue = 24; ///< A row of keys and values.
    int rowSettings = 38; ///< A row of a settings page.
    int rowPalette = 30; ///< A row of the command palette.
    int barTop = 44; ///< The top bar.
    int strip = 34; ///< A tab strip, the history strip, a task's header.
    int statusLine = 26; ///< The status line.
    int rail = 44; ///< The rail's width.
    int pad = 12; ///< A panel's padding.
    int gap = 8; ///< The gap between a panel's parts.
    int sectionHeader = 32; ///< A section's header.
    int chip = 24; ///< A chip.
    int tag = 20; ///< A tag.
    int floatingToolbar = 40; ///< A tool bar floating over an image.
    int scrollBar = 8; ///< A scroll bar's thickness; the same in both densities.

    [[nodiscard]] bool operator==(const MetricTokens&) const = default;
};

struct DesignTokens
{
    ColorTokens colors;
    SpacingTokens spacing;
    RadiusTokens radius;
    LayerTokens layers;
    MaterialTokens materials;
    TypographyTokens typography;
    MetricTokens metrics;

    [[nodiscard]] bool operator==(const DesignTokens&) const = default;
};

/// Everything one resolution of the tokens reads: the theme it resolves, the user's choices and the profile. The
/// application's state is one of these per theme (refreshTokens()); a suite resolves any of them with
/// resolveDesignTokens() and no global.
struct ThemeChoices
{
    ThemeVariant theme = ThemeVariant::Dark;
    ThemeContrastProfile contrast = ThemeContrastProfile::Balanced;
    ThemeAccentFamily accent = ThemeAccentFamily::Blue;
    ThemeSurfaceTintFamily tint = ThemeSurfaceTintFamily::Neutral;
    ThemeTintStrength tintStrength = ThemeTintStrength::Balanced;
    StyleProfile profile = StyleProfile::Classic;
    ThemeDensity density = ThemeDensity::Compact;
    /// Whether Night is on: under the Observatory profile the resolution's last step resolves the dark theme with the
    /// tint off and maps every colour to its luma in red (`THEME_SYSTEM.md`, "Night"). The Classic profile ignores it.
    bool night = false;
    /// Whether the material surfaces take the window system's backdrop (setMaterialBackdropMode()).
    bool nativeBackdrop = false;

    [[nodiscard]] bool operator==(const ThemeChoices&) const = default;
};

/// Resolves the tokens of one combination in the pipeline's order - the base, the accent, the tint, the contrast
/// profile, the text on the accent, the materials, the sizes and the type scale, and Night last - under the
/// combination's profile. Reads and writes no global state, so it may be called from any thread.
[[nodiscard]] AFTERNIGHTCONTROLS_API DesignTokens resolveDesignTokens(const ThemeChoices& choices);

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
AFTERNIGHTCONTROLS_API void setThemeTintStrength(ThemeTintStrength strength);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemeTintStrength themeTintStrength();

/// Puts a density in force and resolves the tokens again. A window shown at the time is sized again when the proxy
/// style refreshes its tokens (AnProxyStyle::refreshThemeTokens()), as a theme change does.
AFTERNIGHTCONTROLS_API void setThemeDensity(ThemeDensity density);
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemeDensity themeDensity();

/// The parts a preset sets; nothing for `Custom`.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<ThemePresetParts> themePresetParts(ThemePreset preset);

/// The preset an accent, a tint and a strength make: the one whose three parts they are, or `Custom`. The settings of
/// an earlier version, which hold no strength, are read with the Balanced one.
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemePreset themePresetOf(ThemeAccentFamily accent, ThemeSurfaceTintFamily tint,
                                                               ThemeTintStrength strength);

/// Puts a preset's three parts in force and resolves the tokens again, once; the theme and the contrast profile are
/// left as they are. `Custom` names no parts and changes nothing.
AFTERNIGHTCONTROLS_API void applyThemePreset(ThemePreset preset);

/// The preset the accent, the tint and the strength in force make.
[[nodiscard]] AFTERNIGHTCONTROLS_API ThemePreset themePreset();

/// Sets Night on or off and resolves the tokens again. Night is never kept with the other choices: it is off at every
/// start unless the user asked to start in it. Switched off, it lets go of the Night pictures it kept.
AFTERNIGHTCONTROLS_API void setNightMode(bool night);

/// Whether Night is set, whatever the profile.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool nightModeSet();

/// Whether Night is in force: set, and the Observatory profile in force. What a widget asks before it paints a picture,
/// and what the viewer asks before it presents one.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isNightMode();

/// Puts a style profile in force and resolves the tokens again. The application sets the profile its shell names once,
/// before its first widget: a widget is sized and painted under one profile for its life. Once a widget exists, a
/// profile other than the one in force is refused, and the refusal is logged.
///
/// @return whether `profile` is in force when the call returns: true when it already was or has been set, false when
///         it was refused.
AFTERNIGHTCONTROLS_API bool setStyleProfile(StyleProfile profile);

/// The style profile in force: `Classic` until setStyleProfile() sets another.
[[nodiscard]] AFTERNIGHTCONTROLS_API StyleProfile styleProfile();

namespace testing
{
/// Puts a style profile in force whatever widgets exist, and resolves the tokens again. For the suites alone, through
/// `an::ui::testing::ScopedStyleProfile` (`fixtures/ScopedStyleProfile.h`), which only the test executables compile: a
/// suite tests a profile in a process whose widgets were made under another. The application calls
/// setStyleProfile().
AFTERNIGHTCONTROLS_API void forceStyleProfile(StyleProfile profile);
} // namespace testing
[[nodiscard]] AFTERNIGHTCONTROLS_API bool nativeBackdropSupported();
[[nodiscard]] AFTERNIGHTCONTROLS_API bool usingNativeBackdropMaterials();

[[nodiscard]] AFTERNIGHTCONTROLS_API bool isDarkTheme();

} // namespace an::ui
