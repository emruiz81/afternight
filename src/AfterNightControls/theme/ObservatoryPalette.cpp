// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/ObservatoryPalette.h"

#include "theme/NightImage.h"

#include <QFont>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{

using an::ui::ThemeAccentFamily;
using an::ui::ThemeContrastProfile;
using an::ui::ThemeSurfaceTintFamily;
using an::ui::ThemeTintStrength;

/// A colour written as the tables write it: `#RRGGBB`.
constexpr QColor hex(const unsigned rgb)
{
    return QColor(static_cast<int>((rgb >> 16u) & 0xFFu), static_cast<int>((rgb >> 8u) & 0xFFu),
                  static_cast<int>(rgb & 0xFFu));
}

/// The nearest whole value, a half rounded up. The tables are written in decimals that binary fractions only approach
/// (0.78 x 63 + 0.22 x 138 is 79.5 exactly and 79.4999... in doubles), so a margin far under any colour step lets the
/// result be the one the decimals give.
int nearest(const double value)
{
    return static_cast<int>(std::floor(value + 0.5 + 1e-7));
}

/// A colour at a percentage of opacity, as the tables write it: `rgba(10, 10, 13, 0.80)` is
/// `atPercent(hex(0x0A0A0D), 80)`.
QColor atPercent(const QColor& color, const double percent)
{
    QColor translucent = color;
    translucent.setAlpha(nearest(percent * 255.0 / 100.0));
    return translucent;
}

// ---- The neutral bases and the text: one row per theme and contrast profile.

/// What a contrast profile sets in a theme: the five neutral surfaces, the three text tones, and the opacity of the
/// line and of the outline, which are the theme's line colour (white in the dark theme, the darkest neutral in the
/// light one) at that percentage. The Soft profile's secondary text is as light (dark theme) or as dark (light theme)
/// as its third surface needs it under the Strong tint, the most tinted that surface gets.
struct NeutralRow
{
    std::array<QColor, 5u> neutral;
    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    double linePercent = 0.0;
    double outlinePercent = 0.0;
};

const NeutralRow& neutralRow(const bool dark, const ThemeContrastProfile contrast)
{
    static const NeutralRow darkBalanced{{hex(0x0E0E11), hex(0x18181B), hex(0x202024), hex(0x29292E), hex(0x34343A)},
                                         hex(0xF1F1F4),
                                         hex(0xB7B9C5),
                                         hex(0x9A9CAA),
                                         9.0,
                                         28.0};
    static const NeutralRow darkSoft{{hex(0x17171B), hex(0x202024), hex(0x28282D), hex(0x313137), hex(0x3B3B42)},
                                     hex(0xE6E6EB),
                                     hex(0xBDBFCB),
                                     hex(0x9A9CAA),
                                     9.0,
                                     28.0};
    static const NeutralRow darkHigh{{hex(0x050506), hex(0x0F0F11), hex(0x17171A), hex(0x202024), hex(0x2C2C31)},
                                     hex(0xFFFFFF),
                                     hex(0xCDCFDA),
                                     hex(0xA4A6B3),
                                     16.0,
                                     46.0};
    static const NeutralRow darkOled{{hex(0x000000), hex(0x000000), hex(0x0C0C0C), hex(0x151515), hex(0x202020)},
                                     hex(0xFFFFFF),
                                     hex(0xB4B4B4),
                                     hex(0x8C8C8C),
                                     16.0,
                                     34.0};
    static const NeutralRow lightBalanced{{hex(0xFCFCFD), hex(0xF4F4F6), hex(0xEAEAED), hex(0xE0E0E4), hex(0xD5D5DA)},
                                          hex(0x18181B),
                                          hex(0x474852),
                                          hex(0x686A76),
                                          13.0,
                                          40.0};
    static const NeutralRow lightSoft{{hex(0xF3F3F5), hex(0xECECEF), hex(0xE3E3E7), hex(0xDADADF), hex(0xD0D0D6)},
                                      hex(0x26262B),
                                      hex(0x50515C),
                                      hex(0x686A76),
                                      13.0,
                                      40.0};
    static const NeutralRow lightHigh{{hex(0xFFFFFF), hex(0xF6F6F8), hex(0xEBEBEE), hex(0xDFDFE4), hex(0xD2D2D8)},
                                      hex(0x000000),
                                      hex(0x33343C),
                                      hex(0x55565F),
                                      24.0,
                                      62.0};

    switch (contrast)
    {
    case ThemeContrastProfile::High:
        return dark ? darkHigh : lightHigh;
    case ThemeContrastProfile::Soft:
        return dark ? darkSoft : lightSoft;
    case ThemeContrastProfile::Oled:
        // OLED is the dark theme's: the light theme resolves it as Balanced, as the Classic profile does.
        return dark ? darkOled : lightBalanced;
    case ThemeContrastProfile::Balanced:
        break;
    }
    return dark ? darkBalanced : lightBalanced;
}

/// What each ladder step takes of the tint's surface share, from surface 0 to surface 4. The dark theme climbs faster:
/// a tint shows less on a dark neutral.
constexpr std::array<double, 5u> kDarkLadder{1.0, 1.5, 2.0, 2.5, 2.5};
constexpr std::array<double, 5u> kLightLadder{0.75, 1.0, 1.5, 1.75, 2.0};

// ---- The tint.

/// The hue a surface tint blends in, the same in both themes.
QColor tintHue(const ThemeSurfaceTintFamily tint)
{
    switch (tint)
    {
    case ThemeSurfaceTintFamily::Ocean:
        return hex(0x3B82F6);
    case ThemeSurfaceTintFamily::Mint:
        return hex(0x14B8A6);
    case ThemeSurfaceTintFamily::Amethyst:
        return hex(0x8B5CF6);
    case ThemeSurfaceTintFamily::Rose:
        return hex(0xF43F5E);
    case ThemeSurfaceTintFamily::Amber:
        return hex(0xF59E0B);
    case ThemeSurfaceTintFamily::Neutral:
        break;
    }
    return {};
}

/// How much of its hue a tint keeps before it is blended; the rest is the lift grey. A raw Rose or Amber turns a dark
/// neutral brown or maroon, so a warm hue is first pulled toward a light grey, and warms the graphite instead.
double tintKept(const bool dark, const ThemeSurfaceTintFamily tint)
{
    switch (tint)
    {
    case ThemeSurfaceTintFamily::Mint:
        return dark ? 0.66 : 1.0;
    case ThemeSurfaceTintFamily::Amethyst:
        return dark ? 0.78 : 1.0;
    case ThemeSurfaceTintFamily::Rose:
        return dark ? 0.44 : 0.78;
    case ThemeSurfaceTintFamily::Amber:
        return dark ? 0.40 : 0.72;
    case ThemeSurfaceTintFamily::Ocean:
    case ThemeSurfaceTintFamily::Neutral:
        break;
    }
    return 1.0;
}

/// The grey a softened tint is pulled toward.
QColor tintLift(const bool dark)
{
    return dark ? hex(0x808086) : hex(0x8A8A90);
}

/// The share of the softened tint that goes into a surface and into a line, as fractions. The Neutral tint is Off
/// whatever the strength.
std::pair<double, double> tintShares(const ThemeSurfaceTintFamily tint, const ThemeTintStrength strength)
{
    if (tint == ThemeSurfaceTintFamily::Neutral)
    {
        return {0.0, 0.0};
    }
    switch (strength)
    {
    case ThemeTintStrength::Off:
        return {0.0, 0.0};
    case ThemeTintStrength::Subtle:
        return {0.05, 0.08};
    case ThemeTintStrength::Strong:
        return {0.12, 0.18};
    case ThemeTintStrength::Balanced:
        break;
    }
    return {0.08, 0.12};
}

// ---- The accents.

/// One accent family's steps 300 to 800.
struct AccentRamp
{
    QColor s300;
    QColor s400;
    QColor s500;
    QColor s600;
    QColor s700;
    QColor s800;
};

AccentRamp accentRamp(const ThemeAccentFamily family)
{
    switch (family)
    {
    case ThemeAccentFamily::Teal:
        return {hex(0x5EEAD4), hex(0x2DD4BF), hex(0x14B8A6), hex(0x0D9488), hex(0x0F766E), hex(0x115E59)};
    case ThemeAccentFamily::Amber:
        return {hex(0xFCD34D), hex(0xFBBF24), hex(0xF59E0B), hex(0xD97706), hex(0xB45309), hex(0x92400E)};
    case ThemeAccentFamily::Amethyst:
        return {hex(0xC4B5FD), hex(0xA78BFA), hex(0x8B5CF6), hex(0x7C3AED), hex(0x6D28D9), hex(0x5B21B6)};
    case ThemeAccentFamily::Rose:
        return {hex(0xFDA4AF), hex(0xFB7185), hex(0xF43F5E), hex(0xE11D48), hex(0xBE123C), hex(0x9F1239)};
    case ThemeAccentFamily::Blue:
        break;
    }
    return {hex(0x93C5FD), hex(0x60A5FA), hex(0x3B82F6), hex(0x2563EB), hex(0x1D4ED8), hex(0x1E40AF)};
}

/// The accent roles a theme takes from a family's steps.
struct AccentRoles
{
    QColor primary;
    QColor hover;
    QColor pressed;
    QColor text;
    QColor onAccent;
};

AccentRoles accentRoles(const bool dark, const ThemeAccentFamily family)
{
    const AccentRamp ramp = accentRamp(family);
    if (dark)
    {
        return {ramp.s400, ramp.s300, ramp.s500, ramp.s300, hex(0x0B1020)};
    }
    // Teal and Amber step one tone darker in the light theme, so white text keeps 4.5 : 1 on the fill.
    if (family == ThemeAccentFamily::Teal || family == ThemeAccentFamily::Amber)
    {
        return {ramp.s700, ramp.s800, ramp.s800, ramp.s800, hex(0xFFFFFF)};
    }
    return {ramp.s600, ramp.s700, ramp.s800, ramp.s700, hex(0xFFFFFF)};
}

/// The opacity of the accent's disabled form, as the Classic profile derives it.
int accentDisabledAlpha(const bool dark)
{
    return dark ? 90 : 96;
}

// ---- Everything else, per theme.

/// The colours a theme sets that no other choice moves, before the tint is mixed into the ones that take it.
struct ThemeBases
{
    QColor success;
    QColor warning;
    QColor error;
    QColor info;
    QColor pinned;
    std::array<QColor, 4u> channels; // red, green, blue, gray
    std::array<QColor, 4u> plots; // red, green, blue, gray
    QColor stateLayer;
    QColor well;
    QColor canvas;
    QColor snack;
    QColor textOnSnack; // invalid: the theme's primary text
    QColor scrim;
    QColor scrimSoft;
    QColor shadow;
    QColor edge;
    double accentContainerShare = 0.0;
};

const ThemeBases& themeBases(const bool dark)
{
    // A plot is drawn in a dark well in both themes, in the same colours.
    static const std::array<QColor, 4u> plotColours{hex(0xFF6B6B), hex(0x5EE1A0), hex(0x6FA8FF), hex(0xC9D3E8)};
    static const ThemeBases darkBases{
        hex(0x4ADE80),
        hex(0xFB923C),
        hex(0xF87171),
        hex(0x7CB8FF),
        hex(0xFBBF24),
        {hex(0xFF6B6B), hex(0x5EE1A0), hex(0x6FA8FF), hex(0xC9D3E8)},
        plotColours,
        hex(0xFFFFFF),
        hex(0x09090B),
        hex(0x060608),
        hex(0x303036),
        QColor(),
        atPercent(hex(0x0A0A0D), 80.0),
        atPercent(hex(0x000000), 38.0),
        atPercent(hex(0x000000), 42.0),
        atPercent(hex(0x000000), 55.0),
        0.28,
    };
    static const ThemeBases lightBases{
        hex(0x15803D),
        hex(0xC2410C),
        hex(0xDC2626),
        hex(0x2563EB),
        hex(0xD97706),
        {hex(0xD64545), hex(0x1E9E63), hex(0x2F6FE0), hex(0x4A5570)},
        plotColours,
        hex(0x18181B),
        hex(0x1A1A1F),
        hex(0x26262B),
        hex(0x2B2B31),
        hex(0xF1F1F4),
        atPercent(hex(0x121216), 78.0),
        atPercent(hex(0x121216), 30.0),
        atPercent(hex(0x18181B), 16.0),
        atPercent(hex(0x18181B), 14.0),
        0.18,
    };
    return dark ? darkBases : lightBases;
}

/// The colours over an image, the same in both themes: an image is judged against dark.
constexpr QColor kTextOnImage = hex(0xF4F6FB);
constexpr QColor kTextOnImageSecondary = hex(0xC2C8D6);
constexpr QColor kTextOnAccentImage = hex(0x0B1020);

/// The opacity of a soft status colour and of the accent's soft forms.
constexpr double kSoftPercent = 16.0;
constexpr double kSofterPercent = 8.0;
constexpr double kAccentLinePercent = 45.0;
/// How much of the accent's text tone the text on the accent container keeps, the rest being the primary text.
constexpr double kOnContainerAccentShare = 0.45;
/// How much of the accent a track takes over the fourth neutral.
constexpr double kTrackAccentShare = 0.30;
/// How many times the surface share of the tint goes into the transient message's fill.
constexpr double kSnackTintSteps = 2.5;

constexpr std::array<const char*, 5u> kLayerNames{"surface0", "surface1", "surface2", "surface3", "surface4"};

QString named(const char* name, const QColor& color)
{
    return QStringLiteral("%1 %2").arg(QString::fromLatin1(name), color.name(QColor::HexRgb).toUpper());
}

// ---- Night.

/// The ratio Night's secondary and tertiary text keep against the second surface, both in red.
constexpr double kNightSecondaryRatio = 3.0;

/// A text tone as Night shows it on `surface`: kept when its luma reaches the ratio against the surface's, both in red;
/// otherwise the grey of the least luma that does, which Night shows as that luma in red.
QColor raisedForNight(const QColor& tone, const QColor& surface)
{
    const QColor ground = an::ui::nightColor(surface);
    const int own = an::ui::nightColor(tone).red();
    int luma = own;
    while (luma < 255 && an::ui::contrastRatio(QColor(luma, 0, 0), ground) + 1e-9 < kNightSecondaryRatio)
    {
        ++luma;
    }
    return luma == own ? tone : QColor(luma, luma, luma);
}

/// Calls `visit` with every colour role of a set, by its name.
template <typename Colours, typename Visit> void forEachColourRole(Colours& c, const Visit& visit)
{
    visit("bgPrimary", c.bgPrimary);
    visit("bgSurface", c.bgSurface);
    visit("bgCard", c.bgCard);
    visit("bgCardPrimary", c.bgCardPrimary);
    visit("bgCardSecondary", c.bgCardSecondary);
    visit("bgInput", c.bgInput);
    visit("bgHover", c.bgHover);
    visit("textPrimary", c.textPrimary);
    visit("textSecondary", c.textSecondary);
    visit("textTertiary", c.textTertiary);
    visit("textAccent", c.textAccent);
    visit("textOnAccent", c.textOnAccent);
    visit("borderPrimary", c.borderPrimary);
    visit("borderSubtle", c.borderSubtle);
    visit("borderFocus", c.borderFocus);
    visit("statusSuccess", c.statusSuccess);
    visit("statusWarning", c.statusWarning);
    visit("statusError", c.statusError);
    visit("statusInfo", c.statusInfo);
    visit("statusSuccessSoft", c.statusSuccessSoft);
    visit("statusWarningSoft", c.statusWarningSoft);
    visit("statusErrorSoft", c.statusErrorSoft);
    visit("statusInfoSoft", c.statusInfoSoft);
    visit("accentPrimary", c.accentPrimary);
    visit("accentHover", c.accentHover);
    visit("accentPressed", c.accentPressed);
    visit("accentDisabled", c.accentDisabled);
    visit("accentPinned", c.accentPinned);
    visit("cardSurfaceTint", c.cardSurfaceTint);
    visit("cardSurfaceBorderTint", c.cardSurfaceBorderTint);
    visit("shadowOverlay", c.shadowOverlay);
    visit("bgCanvas", c.bgCanvas);
    visit("bgWell", c.bgWell);
    visit("bgSnack", c.bgSnack);
    visit("textOnSnack", c.textOnSnack);
    visit("stateLayer", c.stateLayer);
    visit("scrim", c.scrim);
    visit("scrimSoft", c.scrimSoft);
    visit("edge", c.edge);
    visit("textOnImage", c.textOnImage);
    visit("textOnImageSecondary", c.textOnImageSecondary);
    visit("accentOnImage", c.accentOnImage);
    visit("textOnAccentImage", c.textOnAccentImage);
    visit("accentContainer", c.accentContainer);
    visit("textOnAccentContainer", c.textOnAccentContainer);
    visit("accentTrack", c.accentTrack);
    visit("accentSoft", c.accentSoft);
    visit("accentSofter", c.accentSofter);
    visit("accentLine", c.accentLine);
    visit("channelRed", c.channelRed);
    visit("channelGreen", c.channelGreen);
    visit("channelBlue", c.channelBlue);
    visit("channelGray", c.channelGray);
    visit("plotRed", c.plotRed);
    visit("plotGreen", c.plotGreen);
    visit("plotBlue", c.plotBlue);
    visit("plotGray", c.plotGray);
}

/// The density's column of metrics.
an::ui::MetricTokens densityMetrics(const an::ui::ThemeDensity density)
{
    if (density != an::ui::ThemeDensity::Comfortable)
    {
        return an::ui::MetricTokens{.fontBody = 13,
                                    .fontSmall = 12,
                                    .fontCaption = 11,
                                    .control = 32,
                                    .controlSmall = 28,
                                    .controlXSmall = 24,
                                    .controlLarge = 40,
                                    .rowTable = 30,
                                    .rowDense = 28,
                                    .rowKeyValue = 24,
                                    .rowSettings = 38,
                                    .rowPalette = 30,
                                    .barTop = 44,
                                    .strip = 34,
                                    .statusLine = 26,
                                    .rail = 44,
                                    .pad = 12,
                                    .gap = 8,
                                    .sectionHeader = 32,
                                    .chip = 24,
                                    .tag = 20,
                                    .floatingToolbar = 40,
                                    .scrollBar = 8};
    }
    return an::ui::MetricTokens{.fontBody = 14,
                                .fontSmall = 13,
                                .fontCaption = 12,
                                .control = 36,
                                .controlSmall = 32,
                                .controlXSmall = 26,
                                .controlLarge = 44,
                                .rowTable = 36,
                                .rowDense = 32,
                                .rowKeyValue = 28,
                                .rowSettings = 42,
                                .rowPalette = 34,
                                .barTop = 48,
                                .strip = 40,
                                .statusLine = 28,
                                .rail = 44,
                                .pad = 14,
                                .gap = 10,
                                .sectionHeader = 36,
                                .chip = 26,
                                .tag = 22,
                                .floatingToolbar = 44,
                                .scrollBar = 8};
}

/// The radii: small parts 6, controls 8, cards and popups 12, sheets 16. A pill is half its height, which no radius
/// states.
constexpr an::ui::RadiusTokens kRadius{.none = 0, .sm = 6, .md = 8, .lg = 12, .xl = 16};

/// The first two headings' sizes, the same in both densities; the third is the body's size.
constexpr int kHeading1Pixels = 24;
constexpr int kHeading2Pixels = 17;

/// The monospace families, in the order they are looked for; the platform's monospace after them.
constexpr std::array<const char*, 3u> kMonospaceFamilies{"Cascadia Mono", "Cascadia Code", "Consolas"};

/// The logical dots per inch Qt 6 presents to an application on every screen, which a point size is laid out at.
constexpr double kLogicalDotsPerInch = 96.0;

/// `font` at `pixels` logical pixels and `weight`. The size is set as points, so that code that makes a font from
/// another by its point size keeps working; at the logical dots per inch Qt presents, the two are the same size.
QFont sized(QFont font, const int pixels, const QFont::Weight weight)
{
    font.setPointSizeF(pixels * 72.0 / kLogicalDotsPerInch);
    font.setWeight(weight);
    return font;
}

} // namespace

namespace an::ui
{

QColor mixColors(const QColor& a, const double fractionOfA, const QColor& b)
{
    const double p = std::clamp(fractionOfA, 0.0, 1.0);
    const double alphaA = a.alpha() / 255.0;
    const double alphaB = b.alpha() / 255.0;
    const double alpha = p * alphaA + (1.0 - p) * alphaB;
    if (alpha <= 0.0)
    {
        return QColor(0, 0, 0, 0);
    }
    const auto channel = [p, alphaA, alphaB, alpha](const int fromA, const int fromB) {
        const double premultiplied = p * fromA * alphaA + (1.0 - p) * fromB * alphaB;
        return nearest(std::clamp(premultiplied / alpha, 0.0, 255.0));
    };
    return QColor(channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue()),
                  nearest(alpha * 255.0));
}

void resolveObservatoryColors(DesignTokens& tokens, const ThemeChoices& choices)
{
    const bool dark = choices.theme == ThemeVariant::Dark;
    const bool oled = dark && choices.contrast == ThemeContrastProfile::Oled;
    const NeutralRow& row = neutralRow(dark, choices.contrast);
    const ThemeBases& bases = themeBases(dark);

    // The softened tint, and its shares of a surface and of a line.
    const auto [surfaceShare, lineShare] = tintShares(choices.tint, choices.tintStrength);
    const bool tinted = surfaceShare > 0.0;
    const QColor softenedTint =
        tinted ? mixColors(tintHue(choices.tint), tintKept(dark, choices.tint), tintLift(dark)) : QColor();
    const auto withTint = [&softenedTint, tinted](const QColor& base, const double share) {
        return tinted && share > 0.0 ? mixColors(softenedTint, share, base) : base;
    };

    // The ladder: each neutral with its step's share of the tint. Under OLED the first two stay black.
    const std::array<double, 5u>& ladder = dark ? kDarkLadder : kLightLadder;
    std::array<QColor, 5u> surface;
    for (std::size_t i = 0; i < surface.size(); ++i)
    {
        const double steps = oled && i < 2u ? 0.0 : ladder[i];
        surface[i] = withTint(row.neutral[i], surfaceShare * steps);
    }

    const QColor lineColour = dark ? hex(0xFFFFFF) : hex(0x18181B);
    const QColor line = withTint(atPercent(lineColour, row.linePercent), lineShare);
    const QColor outline = withTint(atPercent(lineColour, row.outlinePercent), lineShare);
    AccentRoles accent = accentRoles(dark, choices.accent);
    const AccentRamp ramp = accentRamp(choices.accent);

    // Night's tones, before every colour is mapped to red: the primary text white, the secondary and tertiary text as
    // light as the second surface needs them in red, and the accent's fill its lightest step under black text, so a
    // filled control still reads.
    QColor textPrimary = row.textPrimary;
    QColor textSecondary = row.textSecondary;
    QColor textTertiary = row.textTertiary;
    if (choices.night)
    {
        textPrimary = hex(0xFFFFFF);
        textSecondary = raisedForNight(textSecondary, surface[2]);
        textTertiary = raisedForNight(textTertiary, surface[2]);
        accent.primary = ramp.s300;
        accent.hover = ramp.s300;
        accent.pressed = ramp.s400;
        accent.onAccent = hex(0x000000);
    }

    ColorTokens& c = tokens.colors;
    c.bgPrimary = surface[0];
    c.bgSurface = surface[1];
    c.bgCard = surface[2];
    c.bgCardPrimary = surface[2];
    c.bgCardSecondary = surface[3];
    c.bgInput = surface[3];
    c.bgHover = surface[4];

    c.textPrimary = textPrimary;
    c.textSecondary = textSecondary;
    c.textTertiary = textTertiary;
    c.textAccent = accent.text;
    c.textOnAccent = accent.onAccent;

    c.borderPrimary = outline;
    c.borderSubtle = line;
    c.borderFocus = accent.text;

    c.statusSuccess = bases.success;
    c.statusWarning = bases.warning;
    c.statusError = bases.error;
    c.statusInfo = bases.info;
    c.statusSuccessSoft = atPercent(bases.success, kSoftPercent);
    c.statusWarningSoft = atPercent(bases.warning, kSoftPercent);
    c.statusErrorSoft = atPercent(bases.error, kSoftPercent);
    c.statusInfoSoft = atPercent(bases.info, kSoftPercent);

    c.accentPrimary = accent.primary;
    c.accentHover = accent.hover;
    c.accentPressed = accent.pressed;
    QColor disabled = accent.primary;
    disabled.setAlpha(accentDisabledAlpha(dark));
    c.accentDisabled = disabled;
    c.accentPinned = bases.pinned;
    // The tint a widget may still blend itself: the softened hue, while the surfaces carry any of it.
    c.cardSurfaceTint = tinted ? softenedTint : QColor();
    c.cardSurfaceBorderTint = tinted && lineShare > 0.0 ? softenedTint : QColor();
    c.shadowOverlay = bases.shadow;

    c.bgCanvas = withTint(oled ? hex(0x000000) : bases.canvas, surfaceShare);
    c.bgWell = withTint(oled ? hex(0x000000) : bases.well, surfaceShare);
    c.bgSnack = withTint(bases.snack, surfaceShare * kSnackTintSteps);
    c.textOnSnack = bases.textOnSnack.isValid() ? bases.textOnSnack : textPrimary;
    c.stateLayer = bases.stateLayer;
    c.scrim = bases.scrim;
    c.scrimSoft = bases.scrimSoft;
    c.edge = bases.edge;
    c.textOnImage = kTextOnImage;
    c.textOnImageSecondary = kTextOnImageSecondary;
    c.accentOnImage = ramp.s300;
    c.textOnAccentImage = kTextOnAccentImage;
    // The container and the track mix the accent over the bare neutrals, not the tinted steps: a chosen item and a
    // slider read the same whatever the tint, and the container's text keeps its contrast under every tint.
    c.accentContainer = mixColors(accent.primary, bases.accentContainerShare, row.neutral[3]);
    c.textOnAccentContainer = mixColors(accent.text, kOnContainerAccentShare, textPrimary);
    c.accentTrack = mixColors(accent.primary, kTrackAccentShare, row.neutral[4]);
    c.accentSoft = atPercent(accent.primary, kSoftPercent);
    c.accentSofter = atPercent(accent.primary, kSofterPercent);
    c.accentLine = atPercent(accent.primary, kAccentLinePercent);
    c.channelRed = bases.channels[0];
    c.channelGreen = bases.channels[1];
    c.channelBlue = bases.channels[2];
    c.channelGray = bases.channels[3];
    c.plotRed = bases.plots[0];
    c.plotGreen = bases.plots[1];
    c.plotBlue = bases.plots[2];
    c.plotGray = bases.plots[3];

    LayerTokens& layers = tokens.layers;
    layers.surface0 = surface[0];
    layers.surface1 = surface[1];
    layers.surface2 = surface[2];
    layers.surface3 = surface[3];
    layers.surface4 = surface[4];

    MaterialTokens& materials = tokens.materials;
    materials.solidSurface = surface[1];
    materials.micaSurface = surface[0];
    materials.acrylicSurface = c.bgCard;
}

std::vector<ContrastFailure> observatoryContrastFailures(const DesignTokens& tokens, const ThemeChoices& choices)
{
    std::vector<ContrastFailure> failures;
    const ColorTokens& c = tokens.colors;
    const std::array<QColor, 5u> surface{tokens.layers.surface0, tokens.layers.surface1, tokens.layers.surface2,
                                         tokens.layers.surface3, tokens.layers.surface4};

    // A translucent foreground is laid over its background first; a translucent background over the given ground.
    const auto require = [&failures](const char* foregroundName, const QColor& foreground, const char* backgroundName,
                                     const QColor& background, const double minimum) {
        const QColor shown = compositeOver(foreground, background);
        const double ratio = contrastRatio(shown, background);
        if (ratio + 1e-9 < minimum)
        {
            failures.push_back(ContrastFailure{
                QStringLiteral("%1 on %2").arg(named(foregroundName, foreground), named(backgroundName, background)),
                ratio, minimum});
        }
    };

    if (choices.night)
    {
        // Red carries a fifth of white's luminance, so Night asks less of everything but the primary text, and a
        // status colour never goes without its icon.
        for (std::size_t i = 0; i < 4u; ++i)
        {
            require("textPrimary", c.textPrimary, kLayerNames[i], surface[i], 4.5);
        }
        for (std::size_t i = 0; i < 3u; ++i)
        {
            require("textSecondary", c.textSecondary, kLayerNames[i], surface[i], kNightSecondaryRatio);
            require("textTertiary", c.textTertiary, kLayerNames[i], surface[i], kNightSecondaryRatio);
        }
        require("textOnAccent", c.textOnAccent, "accentPrimary", c.accentPrimary, kNightSecondaryRatio);
        for (std::size_t i = 1; i < 3u; ++i)
        {
            require("statusSuccess", c.statusSuccess, kLayerNames[i], surface[i], 2.0);
            require("statusWarning", c.statusWarning, kLayerNames[i], surface[i], 2.0);
            require("statusError", c.statusError, kLayerNames[i], surface[i], 2.0);
            require("statusInfo", c.statusInfo, kLayerNames[i], surface[i], 2.0);
        }
        // Everything is red: a colour that keeps any green or blue is named.
        forEachColourRole(c, [&failures](const char* name, const QColor& colour) {
            if (colour.isValid() && (colour.green() != 0 || colour.blue() != 0))
            {
                failures.push_back(
                    ContrastFailure{QStringLiteral("%1 keeps a hue").arg(named(name, colour)), 0.0, 0.0});
            }
        });
        return failures;
    }

    for (std::size_t i = 0; i < 4u; ++i)
    {
        require("textPrimary", c.textPrimary, kLayerNames[i], surface[i], 4.5);
        require("textSecondary", c.textSecondary, kLayerNames[i], surface[i], 4.5);
        require("textTertiary", c.textTertiary, kLayerNames[i], surface[i], 3.0);
    }
    require("textOnAccent", c.textOnAccent, "accentPrimary", c.accentPrimary, 4.5);
    require("textOnAccentContainer", c.textOnAccentContainer, "accentContainer", c.accentContainer, 4.5);
    require("textOnImage", c.textOnImage, "scrim over black", compositeOver(c.scrim, QColor(0, 0, 0)), 4.5);
    require("textOnImage", c.textOnImage, "scrim over white", compositeOver(c.scrim, QColor(255, 255, 255)), 4.5);

    const bool high = choices.contrast == ThemeContrastProfile::High;
    for (std::size_t i = 1; i < 4u; ++i)
    {
        require("borderPrimary", c.borderPrimary, kLayerNames[i], surface[i], high ? 3.0 : 1.6);
    }

    const bool oled = choices.theme == ThemeVariant::Dark && choices.contrast == ThemeContrastProfile::Oled;
    for (std::size_t i = 0; i + 1 < surface.size(); ++i)
    {
        if (surface[i] == surface[i + 1] && !(oled && i == 0u))
        {
            failures.push_back(
                ContrastFailure{QStringLiteral("%1 equals %2")
                                    .arg(named(kLayerNames[i], surface[i]), named(kLayerNames[i + 1], surface[i + 1])),
                                1.0, 0.0});
        }
    }

    for (std::size_t i = 1; i < 3u; ++i)
    {
        require("statusSuccess", c.statusSuccess, kLayerNames[i], surface[i], 3.0);
        require("statusWarning", c.statusWarning, kLayerNames[i], surface[i], 3.0);
        require("statusError", c.statusError, kLayerNames[i], surface[i], 3.0);
        require("statusInfo", c.statusInfo, kLayerNames[i], surface[i], 3.0);
    }
    return failures;
}

void mapToNight(DesignTokens& tokens)
{
    forEachColourRole(tokens.colors, [](const char* /*name*/, QColor& colour) { colour = nightColor(colour); });
    for (QColor* layer : {&tokens.layers.surface0, &tokens.layers.surface1, &tokens.layers.surface2,
                          &tokens.layers.surface3, &tokens.layers.surface4, &tokens.materials.solidSurface,
                          &tokens.materials.micaSurface, &tokens.materials.acrylicSurface})
    {
        *layer = nightColor(*layer);
    }
}

void resolveObservatorySizes(DesignTokens& tokens, const ThemeDensity density)
{
    tokens.metrics = densityMetrics(density);
    tokens.radius = kRadius;

    // The interface font is the platform's, the one the base set's body is; every figure in it is tabular.
    QFont interfaceFont = tokens.typography.body;
    interfaceFont.setFeature(QFont::Tag("tnum"), 1);
    const MetricTokens& m = tokens.metrics;
    TypographyTokens& type = tokens.typography;
    type.body = sized(interfaceFont, m.fontBody, QFont::Normal);
    type.caption = sized(interfaceFont, m.fontCaption, QFont::Normal);
    type.heading1 = sized(interfaceFont, kHeading1Pixels, QFont::DemiBold);
    type.heading2 = sized(interfaceFont, kHeading2Pixels, QFont::DemiBold);
    type.heading3 = sized(interfaceFont, m.fontBody, QFont::DemiBold);

    QFont monospace = interfaceFont;
    QStringList families;
    for (const char* family : kMonospaceFamilies)
    {
        families.append(QString::fromLatin1(family));
    }
    monospace.setFamilies(families);
    monospace.setStyleHint(QFont::Monospace);
    type.monospace = sized(monospace, m.fontBody, QFont::Normal);
}

} // namespace an::ui
