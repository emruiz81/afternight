// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory profile's colours, resolved by the pure resolver with no global: the surface ladder (the neutral
// bases, the softened tint, each strength's share, OLED's black steps), the accent tables and what is derived from
// them, the status, channel and image colours, and the contrast rules every combination a user can choose is held to.
// Every expected value is computed by hand from the tables, each mix rounded to the nearest 8-bit value.

#include "theme/AnDesignTokens.h"
#include "theme/ObservatoryPalette.h"

#include <QColor>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <ostream>
#include <utility>
#include <vector>

namespace
{

using an::ui::ColorTokens;
using an::ui::DesignTokens;
using an::ui::StyleProfile;
using an::ui::ThemeAccentFamily;
using an::ui::ThemeChoices;
using an::ui::ThemeContrastProfile;
using an::ui::ThemeSurfaceTintFamily;
using an::ui::ThemeTintStrength;
using an::ui::ThemeVariant;

constexpr std::array<ThemeContrastProfile, 4u> kContrasts{ThemeContrastProfile::High, ThemeContrastProfile::Balanced,
                                                          ThemeContrastProfile::Soft, ThemeContrastProfile::Oled};
constexpr std::array<ThemeAccentFamily, 5u> kAccents{ThemeAccentFamily::Blue, ThemeAccentFamily::Teal,
                                                     ThemeAccentFamily::Amber, ThemeAccentFamily::Amethyst,
                                                     ThemeAccentFamily::Rose};
constexpr std::array<ThemeSurfaceTintFamily, 6u> kTints{
    ThemeSurfaceTintFamily::Neutral,  ThemeSurfaceTintFamily::Ocean, ThemeSurfaceTintFamily::Mint,
    ThemeSurfaceTintFamily::Amethyst, ThemeSurfaceTintFamily::Rose,  ThemeSurfaceTintFamily::Amber};
constexpr std::array<ThemeTintStrength, 4u> kStrengths{ThemeTintStrength::Off, ThemeTintStrength::Subtle,
                                                       ThemeTintStrength::Balanced, ThemeTintStrength::Strong};

QColor rgb(const unsigned value, const int alpha = 255)
{
    return QColor(static_cast<int>((value >> 16u) & 0xFFu), static_cast<int>((value >> 8u) & 0xFFu),
                  static_cast<int>(value & 0xFFu), alpha);
}

DesignTokens observatory(const ThemeVariant theme, const ThemeContrastProfile contrast,
                         const ThemeAccentFamily accent = ThemeAccentFamily::Blue,
                         const ThemeSurfaceTintFamily tint = ThemeSurfaceTintFamily::Neutral,
                         const ThemeTintStrength strength = ThemeTintStrength::Balanced)
{
    return an::ui::resolveDesignTokens(ThemeChoices{.theme = theme,
                                                    .contrast = contrast,
                                                    .accent = accent,
                                                    .tint = tint,
                                                    .tintStrength = strength,
                                                    .profile = StyleProfile::Observatory});
}

std::array<QColor, 5u> ladder(const DesignTokens& tokens)
{
    return {tokens.layers.surface0, tokens.layers.surface1, tokens.layers.surface2, tokens.layers.surface3,
            tokens.layers.surface4};
}

/// A neutral row of the tables: the five surfaces and the three text tones.
struct NeutralRow
{
    ThemeVariant theme;
    ThemeContrastProfile contrast;
    std::array<unsigned, 5u> surfaces;
    std::array<unsigned, 3u> text;
    int lineAlpha;
    int outlineAlpha;
};

const std::array<NeutralRow, 7u> kNeutralRows{{
    {ThemeVariant::Dark,
     ThemeContrastProfile::Balanced,
     {0x0E0E11, 0x18181B, 0x202024, 0x29292E, 0x34343A},
     {0xF1F1F4, 0xB7B9C5, 0x9A9CAA},
     23,
     71},
    {ThemeVariant::Dark,
     ThemeContrastProfile::Soft,
     {0x17171B, 0x202024, 0x28282D, 0x313137, 0x3B3B42},
     {0xE6E6EB, 0xBDBFCB, 0x9A9CAA},
     23,
     71},
    {ThemeVariant::Dark,
     ThemeContrastProfile::High,
     {0x050506, 0x0F0F11, 0x17171A, 0x202024, 0x2C2C31},
     {0xFFFFFF, 0xCDCFDA, 0xA4A6B3},
     41,
     117},
    {ThemeVariant::Dark,
     ThemeContrastProfile::Oled,
     {0x000000, 0x000000, 0x0C0C0C, 0x151515, 0x202020},
     {0xFFFFFF, 0xB4B4B4, 0x8C8C8C},
     41,
     87},
    {ThemeVariant::Light,
     ThemeContrastProfile::Balanced,
     {0xFCFCFD, 0xF4F4F6, 0xEAEAED, 0xE0E0E4, 0xD5D5DA},
     {0x18181B, 0x474852, 0x686A76},
     33,
     102},
    {ThemeVariant::Light,
     ThemeContrastProfile::Soft,
     {0xF3F3F5, 0xECECEF, 0xE3E3E7, 0xDADADF, 0xD0D0D6},
     {0x26262B, 0x50515C, 0x686A76},
     33,
     102},
    {ThemeVariant::Light,
     ThemeContrastProfile::High,
     {0xFFFFFF, 0xF6F6F8, 0xEBEBEE, 0xDFDFE4, 0xD2D2D8},
     {0x000000, 0x33343C, 0x55565F},
     61,
     158},
}};

} // namespace

// ---- The surface ladder.

// With the Neutral tint the surfaces are the bare neutrals of the theme and contrast profile, whatever the strength,
// and the text tones, the line and the outline are the table's.
TEST(ObservatoryLadder, NeutralIsTheBareNeutrals)
{
    for (const NeutralRow& row : kNeutralRows)
    {
        const QColor lineColour = row.theme == ThemeVariant::Dark ? rgb(0xFFFFFF) : rgb(0x18181B);
        for (const ThemeTintStrength strength : kStrengths)
        {
            const DesignTokens tokens = observatory(row.theme, row.contrast, ThemeAccentFamily::Blue,
                                                    ThemeSurfaceTintFamily::Neutral, strength);
            const auto surfaces = ladder(tokens);
            for (std::size_t i = 0; i < surfaces.size(); ++i)
            {
                EXPECT_EQ(surfaces[i], rgb(row.surfaces[i]))
                    << "surface" << i << ", theme " << static_cast<int>(row.theme) << ", contrast "
                    << static_cast<int>(row.contrast);
            }
            EXPECT_EQ(tokens.colors.textPrimary, rgb(row.text[0]));
            EXPECT_EQ(tokens.colors.textSecondary, rgb(row.text[1]));
            EXPECT_EQ(tokens.colors.textTertiary, rgb(row.text[2]));
            QColor line = lineColour;
            line.setAlpha(row.lineAlpha);
            QColor outline = lineColour;
            outline.setAlpha(row.outlineAlpha);
            EXPECT_EQ(tokens.colors.borderSubtle, line) << "contrast " << static_cast<int>(row.contrast);
            EXPECT_EQ(tokens.colors.borderPrimary, outline) << "contrast " << static_cast<int>(row.contrast);
            EXPECT_FALSE(tokens.colors.cardSurfaceTint.isValid());
            EXPECT_FALSE(tokens.colors.cardSurfaceBorderTint.isValid());
        }
    }
}

// The existing roles read the ladder: every widget repaints from them without an edit.
TEST(ObservatoryLadder, TheExistingRolesReadTheLadder)
{
    const DesignTokens tokens = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced, ThemeAccentFamily::Teal,
                                            ThemeSurfaceTintFamily::Mint, ThemeTintStrength::Strong);
    const ColorTokens& c = tokens.colors;
    EXPECT_EQ(c.bgPrimary, tokens.layers.surface0);
    EXPECT_EQ(c.bgSurface, tokens.layers.surface1);
    EXPECT_EQ(c.bgCard, tokens.layers.surface2);
    EXPECT_EQ(c.bgCardPrimary, tokens.layers.surface2);
    EXPECT_EQ(c.bgCardSecondary, tokens.layers.surface3);
    EXPECT_EQ(c.bgInput, tokens.layers.surface3);
    EXPECT_EQ(c.bgHover, tokens.layers.surface4);
    EXPECT_EQ(c.borderFocus, c.textAccent);
    EXPECT_EQ(tokens.materials.solidSurface, tokens.layers.surface1);
    EXPECT_EQ(tokens.materials.micaSurface, tokens.layers.surface0);
    EXPECT_EQ(tokens.materials.acrylicSurface, tokens.layers.surface2);
}

// The worked example: in the dark theme at the Balanced contrast, the Ocean tint at the Balanced strength puts 16 % of
// its hue into surface 2.
TEST(ObservatoryLadder, OceanAtBalancedStrengthPutsSixteenPercentIntoSurfaceTwo)
{
    const auto dark = ladder(observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue,
                                         ThemeSurfaceTintFamily::Ocean));
    EXPECT_EQ(dark[2], rgb(0x243046)); // mix(#3B82F6, 16 %, #202024)
    EXPECT_EQ(dark[4], rgb(0x354460)); // mix(#3B82F6, 20 %, #34343A)

    const auto light = ladder(observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue,
                                          ThemeSurfaceTintFamily::Ocean));
    EXPECT_EQ(light[0], rgb(0xF0F5FD)); // mix(#3B82F6, 6 %, #FCFCFD)
}

// Each strength gives the surfaces its share of the tint: Off none, Subtle 5 %, Balanced 8 %, Strong 12 % at surface 0.
TEST(ObservatoryLadder, EachStrengthTakesItsShare)
{
    const std::array<std::pair<ThemeTintStrength, unsigned>, 4u> expected{{
        {ThemeTintStrength::Off, 0x0E0E11},
        {ThemeTintStrength::Subtle, 0x10141C},
        {ThemeTintStrength::Balanced, 0x121723},
        {ThemeTintStrength::Strong, 0x131C2C},
    }};
    for (const auto& [strength, surface0] : expected)
    {
        const DesignTokens tokens = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced,
                                                ThemeAccentFamily::Blue, ThemeSurfaceTintFamily::Ocean, strength);
        EXPECT_EQ(tokens.layers.surface0, rgb(surface0)) << "strength " << static_cast<int>(strength);
    }

    // A line takes the line share, 12 % at Balanced, mixed into the translucent white with its alpha.
    const DesignTokens balanced = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced,
                                              ThemeAccentFamily::Blue, ThemeSurfaceTintFamily::Ocean);
    EXPECT_EQ(balanced.colors.borderSubtle, QColor(137, 180, 250, 51));

    // Off leaves the tint to no surface and to no widget.
    const DesignTokens off = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue,
                                         ThemeSurfaceTintFamily::Ocean, ThemeTintStrength::Off);
    EXPECT_FALSE(off.colors.cardSurfaceTint.isValid());
    EXPECT_EQ(off.colors.borderSubtle, QColor(255, 255, 255, 23));
}

// A tint keeps a share of its hue before it is blended, the rest the lift grey: warm hues are pulled toward the grey so
// they warm a dark neutral instead of browning it. The softened hue is the tint a widget may still blend.
TEST(ObservatoryLadder, EachTintIsSoftenedBeforeItIsBlended)
{
    const std::array<std::pair<ThemeSurfaceTintFamily, unsigned>, 5u> dark{{
        {ThemeSurfaceTintFamily::Ocean, 0x3B82F6},
        {ThemeSurfaceTintFamily::Mint, 0x39A59B},
        {ThemeSurfaceTintFamily::Amethyst, 0x8964DD},
        {ThemeSurfaceTintFamily::Rose, 0xB36374},
        {ThemeSurfaceTintFamily::Amber, 0xAF8C55},
    }};
    const std::array<std::pair<ThemeSurfaceTintFamily, unsigned>, 5u> light{{
        {ThemeSurfaceTintFamily::Ocean, 0x3B82F6},
        {ThemeSurfaceTintFamily::Mint, 0x14B8A6},
        {ThemeSurfaceTintFamily::Amethyst, 0x8B5CF6},
        {ThemeSurfaceTintFamily::Rose, 0xDD5069},
        {ThemeSurfaceTintFamily::Amber, 0xD79830},
    }};
    for (const auto& [tint, softened] : dark)
    {
        const ColorTokens c =
            observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue, tint).colors;
        EXPECT_EQ(c.cardSurfaceTint, rgb(softened)) << "dark, tint " << static_cast<int>(tint);
        EXPECT_EQ(c.cardSurfaceBorderTint, rgb(softened)) << "dark, tint " << static_cast<int>(tint);
    }
    for (const auto& [tint, softened] : light)
    {
        const ColorTokens c =
            observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue, tint).colors;
        EXPECT_EQ(c.cardSurfaceTint, rgb(softened)) << "light, tint " << static_cast<int>(tint);
    }
}

// Under OLED the first two steps stay black whatever the tint; the steps above them, the well and the canvas take it.
TEST(ObservatoryLadder, OledKeepsTheFirstTwoStepsBlack)
{
    for (const ThemeSurfaceTintFamily tint : kTints)
    {
        for (const ThemeTintStrength strength : kStrengths)
        {
            const auto surfaces = ladder(
                observatory(ThemeVariant::Dark, ThemeContrastProfile::Oled, ThemeAccentFamily::Blue, tint, strength));
            EXPECT_EQ(surfaces[0], rgb(0x000000));
            EXPECT_EQ(surfaces[1], rgb(0x000000));
        }
    }
    const DesignTokens strong = observatory(ThemeVariant::Dark, ThemeContrastProfile::Oled, ThemeAccentFamily::Blue,
                                            ThemeSurfaceTintFamily::Ocean, ThemeTintStrength::Strong);
    EXPECT_EQ(strong.layers.surface2, rgb(0x172844)); // mix(#3B82F6, 24 %, #0C0C0C)
    EXPECT_EQ(strong.colors.bgWell, rgb(0x07101E)); // mix(#3B82F6, 12 %, #000000)
    EXPECT_EQ(strong.colors.bgCanvas, rgb(0x07101E));
}

// OLED is the dark theme's: the light theme resolves it as Balanced, as the Classic profile does.
TEST(ObservatoryLadder, TheLightThemeResolvesOledAsBalanced)
{
    for (const ThemeSurfaceTintFamily tint : kTints)
    {
        EXPECT_TRUE(observatory(ThemeVariant::Light, ThemeContrastProfile::Oled, ThemeAccentFamily::Rose, tint) ==
                    observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced, ThemeAccentFamily::Rose, tint))
            << "tint " << static_cast<int>(tint);
    }
}

// The well, the canvas and the transient message are the theme's bases with the tint: once at the surface share, the
// message two and a half times.
TEST(ObservatoryLadder, TheWellTheCanvasAndTheMessageTakeTheTint)
{
    const ColorTokens neutralDark = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced).colors;
    EXPECT_EQ(neutralDark.bgWell, rgb(0x09090B));
    EXPECT_EQ(neutralDark.bgCanvas, rgb(0x060608));
    EXPECT_EQ(neutralDark.bgSnack, rgb(0x303036));
    EXPECT_EQ(neutralDark.textOnSnack, rgb(0xF1F1F4));
    EXPECT_EQ(observatory(ThemeVariant::Dark, ThemeContrastProfile::High).colors.textOnSnack, rgb(0xFFFFFF))
        << "the message's text is the theme's primary text";
    EXPECT_EQ(observatory(ThemeVariant::Dark, ThemeContrastProfile::Oled).colors.bgWell, rgb(0x000000));
    EXPECT_EQ(observatory(ThemeVariant::Dark, ThemeContrastProfile::Oled).colors.bgCanvas, rgb(0x000000));

    const ColorTokens neutralLight = observatory(ThemeVariant::Light, ThemeContrastProfile::High).colors;
    EXPECT_EQ(neutralLight.bgWell, rgb(0x1A1A1F)) << "a plot is judged against dark in both themes";
    EXPECT_EQ(neutralLight.bgCanvas, rgb(0x26262B));
    EXPECT_EQ(neutralLight.bgSnack, rgb(0x2B2B31));
    EXPECT_EQ(neutralLight.textOnSnack, rgb(0xF1F1F4));

    const ColorTokens ocean = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced, ThemeAccentFamily::Blue,
                                          ThemeSurfaceTintFamily::Ocean)
                                  .colors;
    EXPECT_EQ(ocean.bgSnack, rgb(0x32405C)); // mix(#3B82F6, 20 %, #303036)
}

// ---- The accents.

// Each family's steps fill the accent roles: 400, 300 and 500 in the dark theme with dark text on the fill; 600, 700
// and 800 in the light one with white text, a tone darker for Teal and Amber. The accent over an image is the 300 step
// in both themes.
TEST(ObservatoryAccents, TheTableInBothThemes)
{
    struct Ramp
    {
        ThemeAccentFamily family;
        unsigned s300, s400, s500, s600, s700, s800;
    };
    const std::array<Ramp, 5u> ramps{{
        {ThemeAccentFamily::Blue, 0x93C5FD, 0x60A5FA, 0x3B82F6, 0x2563EB, 0x1D4ED8, 0x1E40AF},
        {ThemeAccentFamily::Teal, 0x5EEAD4, 0x2DD4BF, 0x14B8A6, 0x0D9488, 0x0F766E, 0x115E59},
        {ThemeAccentFamily::Amber, 0xFCD34D, 0xFBBF24, 0xF59E0B, 0xD97706, 0xB45309, 0x92400E},
        {ThemeAccentFamily::Amethyst, 0xC4B5FD, 0xA78BFA, 0x8B5CF6, 0x7C3AED, 0x6D28D9, 0x5B21B6},
        {ThemeAccentFamily::Rose, 0xFDA4AF, 0xFB7185, 0xF43F5E, 0xE11D48, 0xBE123C, 0x9F1239},
    }};
    for (const Ramp& ramp : ramps)
    {
        for (const ThemeContrastProfile contrast : kContrasts)
        {
            const ColorTokens dark = observatory(ThemeVariant::Dark, contrast, ramp.family).colors;
            EXPECT_EQ(dark.accentPrimary, rgb(ramp.s400)) << "family " << static_cast<int>(ramp.family);
            EXPECT_EQ(dark.accentHover, rgb(ramp.s300));
            EXPECT_EQ(dark.accentPressed, rgb(ramp.s500));
            EXPECT_EQ(dark.textAccent, rgb(ramp.s300));
            EXPECT_EQ(dark.borderFocus, rgb(ramp.s300));
            EXPECT_EQ(dark.textOnAccent, rgb(0x0B1020));
            EXPECT_EQ(dark.accentOnImage, rgb(ramp.s300));
            EXPECT_EQ(dark.accentDisabled, rgb(ramp.s400, 90));

            const bool darker = ramp.family == ThemeAccentFamily::Teal || ramp.family == ThemeAccentFamily::Amber;
            const ColorTokens light = observatory(ThemeVariant::Light, contrast, ramp.family).colors;
            EXPECT_EQ(light.accentPrimary, rgb(darker ? ramp.s700 : ramp.s600))
                << "family " << static_cast<int>(ramp.family);
            EXPECT_EQ(light.accentHover, rgb(darker ? ramp.s800 : ramp.s700));
            EXPECT_EQ(light.accentPressed, rgb(ramp.s800));
            EXPECT_EQ(light.textAccent, rgb(darker ? ramp.s800 : ramp.s700));
            EXPECT_EQ(light.borderFocus, light.textAccent);
            EXPECT_EQ(light.textOnAccent, rgb(0xFFFFFF));
            EXPECT_EQ(light.accentOnImage, rgb(ramp.s300));
            EXPECT_EQ(light.accentDisabled, rgb(darker ? ramp.s700 : ramp.s600, 96));
        }
    }
}

// The container mixes the accent into the third neutral (28 % dark, 18 % light), its text is the accent's text tone at
// 45 % over the primary text, the track the accent at 30 % over the fourth neutral, and the soft forms the accent at
// 16, 8 and 45 %. The container and the track are the same whatever the tint.
TEST(ObservatoryAccents, TheContainerTheTrackAndTheSoftFormsAreDerived)
{
    const ColorTokens dark = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced).colors;
    EXPECT_EQ(dark.accentContainer, rgb(0x384C67)); // mix(#60A5FA, 28 %, #29292E)
    EXPECT_EQ(dark.textOnAccentContainer, rgb(0xC7DDF8)); // mix(#93C5FD, 45 %, #F1F1F4)
    EXPECT_EQ(dark.accentTrack, rgb(0x415674)); // mix(#60A5FA, 30 %, #34343A)
    EXPECT_EQ(dark.accentSoft, rgb(0x60A5FA, 41));
    EXPECT_EQ(dark.accentSofter, rgb(0x60A5FA, 20));
    EXPECT_EQ(dark.accentLine, rgb(0x60A5FA, 115));

    const ColorTokens light =
        observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced, ThemeAccentFamily::Teal).colors;
    EXPECT_EQ(light.accentContainer, rgb(0xBACDCF)); // mix(#0F766E, 18 %, #E0E0E4)
    EXPECT_EQ(light.textOnAccentContainer, rgb(0x153837)); // mix(#115E59, 45 %, #18181B)
    EXPECT_EQ(light.accentTrack, rgb(0x9AB9BA)); // mix(#0F766E, 30 %, #D5D5DA)
    EXPECT_EQ(light.accentSoft, rgb(0x0F766E, 41));

    for (const ThemeSurfaceTintFamily tint : kTints)
    {
        const ColorTokens tinted = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced,
                                               ThemeAccentFamily::Blue, tint, ThemeTintStrength::Strong)
                                       .colors;
        EXPECT_EQ(tinted.accentContainer, dark.accentContainer) << "tint " << static_cast<int>(tint);
        EXPECT_EQ(tinted.accentTrack, dark.accentTrack) << "tint " << static_cast<int>(tint);
    }
}

// ---- The status, channel and image colours.

// The status colours and their soft forms at 16 %, the pinned colour, the channels' dots and the curves in a well.
TEST(ObservatoryStatus, TheTableInBothThemes)
{
    const ColorTokens dark = observatory(ThemeVariant::Dark, ThemeContrastProfile::Soft, ThemeAccentFamily::Amber,
                                         ThemeSurfaceTintFamily::Rose, ThemeTintStrength::Strong)
                                 .colors;
    EXPECT_EQ(dark.statusSuccess, rgb(0x4ADE80));
    EXPECT_EQ(dark.statusWarning, rgb(0xFB923C));
    EXPECT_EQ(dark.statusError, rgb(0xF87171));
    EXPECT_EQ(dark.statusInfo, rgb(0x7CB8FF)) << "the information colour is the theme's, not the accent's";
    EXPECT_EQ(dark.statusSuccessSoft, rgb(0x4ADE80, 41));
    EXPECT_EQ(dark.statusWarningSoft, rgb(0xFB923C, 41));
    EXPECT_EQ(dark.statusErrorSoft, rgb(0xF87171, 41));
    EXPECT_EQ(dark.statusInfoSoft, rgb(0x7CB8FF, 41));
    EXPECT_EQ(dark.accentPinned, rgb(0xFBBF24));
    EXPECT_EQ(dark.channelRed, rgb(0xFF6B6B));
    EXPECT_EQ(dark.channelGreen, rgb(0x5EE1A0));
    EXPECT_EQ(dark.channelBlue, rgb(0x6FA8FF));
    EXPECT_EQ(dark.channelGray, rgb(0xC9D3E8));

    const ColorTokens light = observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced).colors;
    EXPECT_EQ(light.statusSuccess, rgb(0x15803D));
    EXPECT_EQ(light.statusWarning, rgb(0xC2410C));
    EXPECT_EQ(light.statusError, rgb(0xDC2626));
    EXPECT_EQ(light.statusInfo, rgb(0x2563EB));
    EXPECT_EQ(light.statusErrorSoft, rgb(0xDC2626, 41));
    EXPECT_EQ(light.accentPinned, rgb(0xD97706));
    EXPECT_EQ(light.channelRed, rgb(0xD64545));
    EXPECT_EQ(light.channelGreen, rgb(0x1E9E63));
    EXPECT_EQ(light.channelBlue, rgb(0x2F6FE0));
    EXPECT_EQ(light.channelGray, rgb(0x4A5570));

    // A curve is drawn in a well, dark in both themes, so it keeps its colours.
    for (const ColorTokens* c : {&dark, &light})
    {
        EXPECT_EQ(c->plotRed, rgb(0xFF6B6B));
        EXPECT_EQ(c->plotGreen, rgb(0x5EE1A0));
        EXPECT_EQ(c->plotBlue, rgb(0x6FA8FF));
        EXPECT_EQ(c->plotGray, rgb(0xC9D3E8));
    }
}

// What is laid over an image is the same in both themes, an image being judged against dark; the overlays, the shadow
// and the edge are the theme's.
TEST(ObservatoryStatus, TheColoursOverAnImageAndTheOverlays)
{
    const ColorTokens dark = observatory(ThemeVariant::Dark, ThemeContrastProfile::Balanced).colors;
    const ColorTokens light = observatory(ThemeVariant::Light, ThemeContrastProfile::Balanced).colors;
    for (const ColorTokens* c : {&dark, &light})
    {
        EXPECT_EQ(c->textOnImage, rgb(0xF4F6FB));
        EXPECT_EQ(c->textOnImageSecondary, rgb(0xC2C8D6));
        EXPECT_EQ(c->textOnAccentImage, rgb(0x0B1020));
    }
    EXPECT_EQ(dark.stateLayer, rgb(0xFFFFFF));
    EXPECT_EQ(dark.scrim, rgb(0x0A0A0D, 204));
    EXPECT_EQ(dark.scrimSoft, rgb(0x000000, 97));
    EXPECT_EQ(dark.shadowOverlay, rgb(0x000000, 107));
    EXPECT_EQ(dark.edge, rgb(0x000000, 140));
    EXPECT_EQ(light.stateLayer, rgb(0x18181B));
    EXPECT_EQ(light.scrim, rgb(0x121216, 199));
    EXPECT_EQ(light.scrimSoft, rgb(0x121216, 77));
    EXPECT_EQ(light.shadowOverlay, rgb(0x18181B, 41));
    EXPECT_EQ(light.edge, rgb(0x18181B, 36));
}

// ---- The rules.

// Every combination a user can choose - two themes, four contrast profiles, five accents, six tints, four strengths -
// meets the profile's contrast rules; a combination that misses one is named with the pair that missed.
TEST(ObservatoryValidation, EveryCombinationMeetsTheContrastRules)
{
    int combinations = 0;
    QStringList misses;
    for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
    {
        for (const ThemeContrastProfile contrast : kContrasts)
        {
            for (const ThemeAccentFamily accent : kAccents)
            {
                for (const ThemeSurfaceTintFamily tint : kTints)
                {
                    for (const ThemeTintStrength strength : kStrengths)
                    {
                        const ThemeChoices choices{.theme = theme,
                                                   .contrast = contrast,
                                                   .accent = accent,
                                                   .tint = tint,
                                                   .tintStrength = strength,
                                                   .profile = StyleProfile::Observatory};
                        ++combinations;
                        for (const an::ui::ContrastFailure& failure :
                             an::ui::observatoryContrastFailures(an::ui::resolveDesignTokens(choices), choices))
                        {
                            misses.append(QStringLiteral("[%1 %2 %3 %4 %5] %6: %7 : 1, under %8 : 1")
                                              .arg(static_cast<int>(theme))
                                              .arg(static_cast<int>(contrast))
                                              .arg(static_cast<int>(accent))
                                              .arg(static_cast<int>(tint))
                                              .arg(static_cast<int>(strength))
                                              .arg(failure.pair)
                                              .arg(failure.ratio, 0, 'f', 2)
                                              .arg(failure.minimum, 0, 'f', 2));
                        }
                    }
                }
            }
        }
    }
    EXPECT_EQ(combinations, 960);
    EXPECT_TRUE(misses.isEmpty()) << misses.size() << " pairs miss their rule:\n"
                                  << misses.join(QLatin1Char('\n')).toStdString();
}

// The rules bite: a set whose secondary text is its surface, or whose two steps meet, is named.
TEST(ObservatoryValidation, ARuleTheSetMissesIsNamed)
{
    const ThemeChoices choices{
        .theme = ThemeVariant::Dark, .contrast = ThemeContrastProfile::Balanced, .profile = StyleProfile::Observatory};
    DesignTokens tokens = an::ui::resolveDesignTokens(choices);
    ASSERT_TRUE(an::ui::observatoryContrastFailures(tokens, choices).empty());

    tokens.colors.textSecondary = tokens.layers.surface3;
    tokens.layers.surface2 = tokens.layers.surface1;
    const std::vector<an::ui::ContrastFailure> failures = an::ui::observatoryContrastFailures(tokens, choices);

    bool secondaryNamed = false;
    bool stepsNamed = false;
    for (const an::ui::ContrastFailure& failure : failures)
    {
        secondaryNamed = secondaryNamed || failure.pair.startsWith(QStringLiteral("textSecondary"));
        stepsNamed = stepsNamed || (failure.pair.contains(QStringLiteral("surface1")) &&
                                    failure.pair.contains(QStringLiteral("equals surface2")));
    }
    EXPECT_TRUE(secondaryNamed);
    EXPECT_TRUE(stepsNamed);
}
