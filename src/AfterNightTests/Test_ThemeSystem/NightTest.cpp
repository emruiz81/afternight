// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Night, the resolution's last step under the Observatory profile: the dark theme whatever the preference, the tint off
// whatever the tint and its strength, the primary text white, the secondary and tertiary text raised to 3 : 1 against
// the second surface, the accent's fill its 300 step under black text, and every colour then mapped to its luma in red.
// The Night rules hold in every combination a user can choose; the user's tint is theirs again when Night goes; the
// Classic profile has no Night.

#include "fixtures/ScopedNightMode.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "theme/ObservatoryPalette.h"

#include <QColor>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <array>
#include <functional>
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

ThemeChoices nightChoices(const ThemeVariant theme, const ThemeContrastProfile contrast, const ThemeAccentFamily accent,
                          const ThemeSurfaceTintFamily tint = ThemeSurfaceTintFamily::Neutral,
                          const ThemeTintStrength strength = ThemeTintStrength::Balanced)
{
    return ThemeChoices{.theme = theme,
                        .contrast = contrast,
                        .accent = accent,
                        .tint = tint,
                        .tintStrength = strength,
                        .profile = StyleProfile::Observatory,
                        .night = true};
}

/// Every colour a token set holds: its roles, its surfaces and its materials.
std::vector<std::pair<QString, QColor>> everyColour(const DesignTokens& tokens)
{
    const ColorTokens& c = tokens.colors;
    std::vector<std::pair<QString, QColor>> colours{
        {QStringLiteral("bgPrimary"), c.bgPrimary},
        {QStringLiteral("bgSurface"), c.bgSurface},
        {QStringLiteral("bgCard"), c.bgCard},
        {QStringLiteral("bgCardPrimary"), c.bgCardPrimary},
        {QStringLiteral("bgCardSecondary"), c.bgCardSecondary},
        {QStringLiteral("bgInput"), c.bgInput},
        {QStringLiteral("bgHover"), c.bgHover},
        {QStringLiteral("textPrimary"), c.textPrimary},
        {QStringLiteral("textSecondary"), c.textSecondary},
        {QStringLiteral("textTertiary"), c.textTertiary},
        {QStringLiteral("textAccent"), c.textAccent},
        {QStringLiteral("textOnAccent"), c.textOnAccent},
        {QStringLiteral("borderPrimary"), c.borderPrimary},
        {QStringLiteral("borderSubtle"), c.borderSubtle},
        {QStringLiteral("borderFocus"), c.borderFocus},
        {QStringLiteral("statusSuccess"), c.statusSuccess},
        {QStringLiteral("statusWarning"), c.statusWarning},
        {QStringLiteral("statusError"), c.statusError},
        {QStringLiteral("statusInfo"), c.statusInfo},
        {QStringLiteral("statusSuccessSoft"), c.statusSuccessSoft},
        {QStringLiteral("statusWarningSoft"), c.statusWarningSoft},
        {QStringLiteral("statusErrorSoft"), c.statusErrorSoft},
        {QStringLiteral("statusInfoSoft"), c.statusInfoSoft},
        {QStringLiteral("accentPrimary"), c.accentPrimary},
        {QStringLiteral("accentHover"), c.accentHover},
        {QStringLiteral("accentPressed"), c.accentPressed},
        {QStringLiteral("accentDisabled"), c.accentDisabled},
        {QStringLiteral("accentPinned"), c.accentPinned},
        {QStringLiteral("shadowOverlay"), c.shadowOverlay},
        {QStringLiteral("bgCanvas"), c.bgCanvas},
        {QStringLiteral("bgWell"), c.bgWell},
        {QStringLiteral("bgSnack"), c.bgSnack},
        {QStringLiteral("textOnSnack"), c.textOnSnack},
        {QStringLiteral("stateLayer"), c.stateLayer},
        {QStringLiteral("scrim"), c.scrim},
        {QStringLiteral("scrimSoft"), c.scrimSoft},
        {QStringLiteral("edge"), c.edge},
        {QStringLiteral("textOnImage"), c.textOnImage},
        {QStringLiteral("textOnImageSecondary"), c.textOnImageSecondary},
        {QStringLiteral("accentOnImage"), c.accentOnImage},
        {QStringLiteral("textOnAccentImage"), c.textOnAccentImage},
        {QStringLiteral("accentContainer"), c.accentContainer},
        {QStringLiteral("textOnAccentContainer"), c.textOnAccentContainer},
        {QStringLiteral("accentTrack"), c.accentTrack},
        {QStringLiteral("accentSoft"), c.accentSoft},
        {QStringLiteral("accentSofter"), c.accentSofter},
        {QStringLiteral("accentLine"), c.accentLine},
        {QStringLiteral("channelRed"), c.channelRed},
        {QStringLiteral("channelGreen"), c.channelGreen},
        {QStringLiteral("channelBlue"), c.channelBlue},
        {QStringLiteral("channelGray"), c.channelGray},
        {QStringLiteral("plotRed"), c.plotRed},
        {QStringLiteral("plotGreen"), c.plotGreen},
        {QStringLiteral("plotBlue"), c.plotBlue},
        {QStringLiteral("plotGray"), c.plotGray},
        {QStringLiteral("surface0"), tokens.layers.surface0},
        {QStringLiteral("surface1"), tokens.layers.surface1},
        {QStringLiteral("surface2"), tokens.layers.surface2},
        {QStringLiteral("surface3"), tokens.layers.surface3},
        {QStringLiteral("surface4"), tokens.layers.surface4},
        {QStringLiteral("solidSurface"), tokens.materials.solidSurface},
        {QStringLiteral("micaSurface"), tokens.materials.micaSurface},
        {QStringLiteral("acrylicSurface"), tokens.materials.acrylicSurface},
    };
    if (c.cardSurfaceTint.isValid())
    {
        colours.emplace_back(QStringLiteral("cardSurfaceTint"), c.cardSurfaceTint);
    }
    if (c.cardSurfaceBorderTint.isValid())
    {
        colours.emplace_back(QStringLiteral("cardSurfaceBorderTint"), c.cardSurfaceBorderTint);
    }
    return colours;
}

/// Calls `visit` with every combination a user can choose and Night on: both themes, every contrast profile, accent,
/// tint and strength.
void forEveryNightCombination(const std::function<void(const ThemeChoices&)>& visit)
{
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
                        visit(nightChoices(theme, contrast, accent, tint, strength));
                    }
                }
            }
        }
    }
}

QString named(const ThemeChoices& choices)
{
    return QStringLiteral("[%1 %2 %3 %4 %5]")
        .arg(static_cast<int>(choices.theme))
        .arg(static_cast<int>(choices.contrast))
        .arg(static_cast<int>(choices.accent))
        .arg(static_cast<int>(choices.tint))
        .arg(static_cast<int>(choices.tintStrength));
}

} // namespace

// A colour's Night form is its luma in red - 0.299 R + 0.587 G + 0.114 B on the 8-bit components, to the nearest whole
// value - with its alpha kept.
TEST(Night, AColourBecomesItsLumaInRed)
{
    EXPECT_EQ(an::ui::nightColor(QColor(0x93, 0xC5, 0xFD)), QColor(188, 0, 0)); // Blue 300
    EXPECT_EQ(an::ui::nightColor(QColor(255, 255, 255)), QColor(255, 0, 0));
    EXPECT_EQ(an::ui::nightColor(QColor(0, 0, 0)), QColor(0, 0, 0));
    EXPECT_EQ(an::ui::nightColor(QColor(0, 255, 0)), QColor(150, 0, 0));
    EXPECT_EQ(an::ui::nightColor(QColor(0, 0, 255)), QColor(29, 0, 0));
    EXPECT_EQ(an::ui::nightColor(QColor(10, 10, 13, 204)), QColor(10, 0, 0, 204)) << "the alpha is kept";
}

// Every colour of every combination has no green and no blue under Night.
TEST(Night, EveryColourTokenHasNoGreenOrBlue)
{
    int combinations = 0;
    QStringList coloured;
    forEveryNightCombination([&](const ThemeChoices& choices) {
        ++combinations;
        for (const auto& [name, colour] : everyColour(an::ui::resolveDesignTokens(choices)))
        {
            if (colour.green() != 0 || colour.blue() != 0)
            {
                coloured.append(named(choices) + QLatin1Char(' ') + name + QLatin1Char(' ') +
                                colour.name(QColor::HexArgb));
            }
        }
    });
    EXPECT_EQ(combinations, 960);
    EXPECT_TRUE(coloured.isEmpty()) << coloured.size() << " colours keep a hue:\n"
                                    << coloured.mid(0, 40).join(QLatin1Char('\n')).toStdString();
}

// Night resolves the dark theme whatever the preference, and its tint is off whatever the tint and the strength: every
// combination of a contrast profile and an accent resolves one set.
TEST(Night, TheThemeTheTintAndItsStrengthChangeNoToken)
{
    for (const ThemeContrastProfile contrast : kContrasts)
    {
        for (const ThemeAccentFamily accent : kAccents)
        {
            const DesignTokens reference =
                an::ui::resolveDesignTokens(nightChoices(ThemeVariant::Dark, contrast, accent));
            forEveryNightCombination([&](const ThemeChoices& choices) {
                if (choices.contrast == contrast && choices.accent == accent)
                {
                    EXPECT_TRUE(an::ui::resolveDesignTokens(choices) == reference) << named(choices).toStdString();
                }
            });
        }
    }
}

// Under Night the text keeps its rules in the red it is shown in: primary text 4.5 : 1 against surfaces 0 to 3, the
// secondary and tertiary text and the text on the accent 3 : 1, the status colours 2 : 1 against surfaces 1 and 2.
TEST(Night, EveryCombinationMeetsTheNightRules)
{
    QStringList misses;
    int combinations = 0;
    forEveryNightCombination([&](const ThemeChoices& choices) {
        ++combinations;
        for (const an::ui::ContrastFailure& failure :
             an::ui::observatoryContrastFailures(an::ui::resolveDesignTokens(choices), choices))
        {
            misses.append(QStringLiteral("%1 %2: %3 : 1, under %4 : 1")
                              .arg(named(choices), failure.pair)
                              .arg(failure.ratio, 0, 'f', 2)
                              .arg(failure.minimum, 0, 'f', 2));
        }
    });
    EXPECT_EQ(combinations, 960);
    EXPECT_TRUE(misses.isEmpty()) << misses.size() << " pairs miss their rule:\n"
                                  << misses.mid(0, 40).join(QLatin1Char('\n')).toStdString();
}

// The Night rules bite: a secondary text left at its surface, or green left in a colour, is named.
TEST(Night, ARuleTheSetMissesIsNamed)
{
    const ThemeChoices choices = nightChoices(ThemeVariant::Dark, ThemeContrastProfile::Soft, ThemeAccentFamily::Blue);
    DesignTokens tokens = an::ui::resolveDesignTokens(choices);
    ASSERT_TRUE(an::ui::observatoryContrastFailures(tokens, choices).empty());

    tokens.colors.textSecondary = tokens.layers.surface2;
    tokens.colors.statusError = QColor(0, 200, 0);
    bool secondaryNamed = false;
    bool hueNamed = false;
    for (const an::ui::ContrastFailure& failure : an::ui::observatoryContrastFailures(tokens, choices))
    {
        secondaryNamed = secondaryNamed || failure.pair.startsWith(QStringLiteral("textSecondary"));
        hueNamed = hueNamed || (failure.pair.startsWith(QStringLiteral("statusError #00C800")) &&
                                failure.pair.endsWith(QStringLiteral("keeps a hue")));
    }
    EXPECT_TRUE(secondaryNamed);
    EXPECT_TRUE(hueNamed);
}

// The worked values: white text on the Soft profile's third surface, black on the Blue accent's 300 step, the error
// colour on the Soft profile's second surface; the secondary and tertiary text of the Soft profile raised to the red
// the second surface needs, where they meet.
TEST(Night, TheWorkedValues)
{
    const DesignTokens soft = an::ui::resolveDesignTokens(
        nightChoices(ThemeVariant::Dark, ThemeContrastProfile::Soft, ThemeAccentFamily::Blue));
    const ColorTokens& c = soft.colors;
    EXPECT_EQ(c.textPrimary, QColor(255, 0, 0));
    EXPECT_EQ(soft.layers.surface3, an::ui::nightColor(QColor(0x31, 0x31, 0x37)));
    EXPECT_NEAR(an::ui::contrastRatio(c.textPrimary, soft.layers.surface3), 4.62, 0.005);
    EXPECT_EQ(c.accentPrimary, QColor(188, 0, 0)) << "the accent's fill is its 300 step";
    EXPECT_EQ(c.textOnAccent, QColor(0, 0, 0));
    EXPECT_NEAR(an::ui::contrastRatio(c.textOnAccent, c.accentPrimary), 3.14, 0.005);
    EXPECT_NEAR(an::ui::contrastRatio(c.statusError, soft.layers.surface2), 2.15, 0.005);
    EXPECT_EQ(c.textSecondary, QColor(194, 0, 0));
    EXPECT_EQ(c.textTertiary, QColor(194, 0, 0));

    // A tone that already reaches the rule is not raised: the High profile's secondary text keeps its own luma.
    const DesignTokens high = an::ui::resolveDesignTokens(
        nightChoices(ThemeVariant::Dark, ThemeContrastProfile::High, ThemeAccentFamily::Blue));
    EXPECT_EQ(high.colors.textSecondary, an::ui::nightColor(QColor(0xCD, 0xCF, 0xDA)));
}

// The Classic profile has no Night: the resolver ignores it there, and Night set under it is not in force.
TEST(Night, TheClassicProfileHasNoNight)
{
    ThemeChoices classic{.theme = ThemeVariant::Light, .contrast = ThemeContrastProfile::Balanced};
    const DesignTokens day = an::ui::resolveDesignTokens(classic);
    classic.night = true;
    EXPECT_TRUE(an::ui::resolveDesignTokens(classic) == day);

    const an::ui::testing::ScopedStyleProfile classicProfile(StyleProfile::Classic);
    const an::ui::testing::ScopedNightMode night(true);
    EXPECT_FALSE(an::ui::isNightMode());
}

// Night in force resolves the application's tokens as the resolver does, makes the theme dark, and gives the user's
// tint and strength back when it goes.
TEST(Night, TheUsersChoicesReturnWhenNightGoes)
{
    const an::ui::testing::ScopedStyleProfile observatory(StyleProfile::Observatory);
    const an::ui::ThemePreference preference = an::ui::themePreference();
    const ThemeSurfaceTintFamily tint = an::ui::themeSurfaceTintFamily();
    const ThemeTintStrength strength = an::ui::themeTintStrength();
    an::ui::setThemePreference(an::ui::ThemePreference::Light);
    an::ui::setThemeSurfaceTintFamily(ThemeSurfaceTintFamily::Ocean);
    an::ui::setThemeTintStrength(ThemeTintStrength::Strong);
    const DesignTokens day = an::ui::tokens();
    ASSERT_FALSE(an::ui::isDarkTheme());
    {
        const an::ui::testing::ScopedNightMode night(true);
        EXPECT_TRUE(an::ui::isNightMode());
        EXPECT_TRUE(an::ui::isDarkTheme()) << "Night is dark whatever the preference";
        EXPECT_EQ(an::ui::tokens().colors.bgPrimary.green(), 0);
        EXPECT_EQ(an::ui::tokens().colors.bgPrimary.blue(), 0);
        EXPECT_EQ(an::ui::themeSurfaceTintFamily(), ThemeSurfaceTintFamily::Ocean) << "Night changed the user's tint";
        EXPECT_EQ(an::ui::themeTintStrength(), ThemeTintStrength::Strong);
    }
    EXPECT_FALSE(an::ui::isNightMode());
    EXPECT_FALSE(an::ui::isDarkTheme());
    EXPECT_TRUE(an::ui::tokens() == day) << "the day's tokens did not come back";

    an::ui::setThemeSurfaceTintFamily(tint);
    an::ui::setThemeTintStrength(strength);
    an::ui::setThemePreference(preference);
}
