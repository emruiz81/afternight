// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The colour presets: each names an accent, a tint and a strength, and leaves the theme and the contrast profile to the
// user; a combination no preset names is Custom, so a part changed by hand leaves the preset; the settings of an
// earlier version, which hold an accent and a tint and no strength, are read as the preset they make or as Custom; and
// the names a setting stores.

#include "app/ThemeManager.h"
#include "theme/AnDesignTokens.h"

#include <QString>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <tuple>

namespace
{

using an::ui::ThemeAccentFamily;
using an::ui::ThemeContrastProfile;
using an::ui::ThemePreset;
using an::ui::ThemePresetParts;
using an::ui::ThemeSurfaceTintFamily;
using an::ui::ThemeTintStrength;

/// The six presets with their parts, as the table states them.
const std::array<std::tuple<ThemePreset, ThemeAccentFamily, ThemeSurfaceTintFamily, ThemeTintStrength>, 6u> kTable{{
    {ThemePreset::SlateBlue, ThemeAccentFamily::Blue, ThemeSurfaceTintFamily::Neutral, ThemeTintStrength::Balanced},
    {ThemePreset::Observatory, ThemeAccentFamily::Amber, ThemeSurfaceTintFamily::Ocean, ThemeTintStrength::Strong},
    {ThemePreset::DeepSea, ThemeAccentFamily::Teal, ThemeSurfaceTintFamily::Ocean, ThemeTintStrength::Balanced},
    {ThemePreset::Graphite, ThemeAccentFamily::Amber, ThemeSurfaceTintFamily::Neutral, ThemeTintStrength::Balanced},
    {ThemePreset::Nebula, ThemeAccentFamily::Rose, ThemeSurfaceTintFamily::Amethyst, ThemeTintStrength::Balanced},
    {ThemePreset::Aurora, ThemeAccentFamily::Teal, ThemeSurfaceTintFamily::Mint, ThemeTintStrength::Subtle},
}};

/// The choices a case changes, put back when it ends.
class PartsRestored final
{
public:
    PartsRestored()
        : m_contrast(an::ui::themeContrastProfile()), m_accent(an::ui::themeAccentFamily()),
          m_tint(an::ui::themeSurfaceTintFamily()), m_strength(an::ui::themeTintStrength())
    {
    }

    ~PartsRestored()
    {
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
        an::ui::setThemeTintStrength(m_strength);
    }

    PartsRestored(const PartsRestored&) = delete;
    PartsRestored& operator=(const PartsRestored&) = delete;

private:
    ThemeContrastProfile m_contrast;
    ThemeAccentFamily m_accent;
    ThemeSurfaceTintFamily m_tint;
    ThemeTintStrength m_strength;
};

} // namespace

// Each preset is its three parts; Custom has none.
TEST(Presets, EachPresetIsItsThreeParts)
{
    for (const auto& [preset, accent, tint, strength] : kTable)
    {
        const std::optional<ThemePresetParts> parts = an::ui::themePresetParts(preset);
        ASSERT_TRUE(parts.has_value()) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(parts->accent, accent) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(parts->tint, tint) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(parts->strength, strength) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(an::ui::themePresetOf(accent, tint, strength), preset);
    }
    EXPECT_FALSE(an::ui::themePresetParts(ThemePreset::Custom).has_value());
}

// Applying a preset puts its three parts in force and leaves the contrast profile the user's; the preset in force is
// the one the parts make.
TEST(Presets, ApplyingAPresetSetsItsPartsAndNothingElse)
{
    const PartsRestored restored;
    an::ui::setThemeContrastProfile(ThemeContrastProfile::Soft);
    for (const auto& [preset, accent, tint, strength] : kTable)
    {
        an::ui::applyThemePreset(preset);
        EXPECT_EQ(an::ui::themeAccentFamily(), accent) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(an::ui::themeSurfaceTintFamily(), tint) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(an::ui::themeTintStrength(), strength) << "preset " << static_cast<int>(preset);
        EXPECT_EQ(an::ui::themeContrastProfile(), ThemeContrastProfile::Soft) << "a preset moved the contrast";
        EXPECT_EQ(an::ui::themePreset(), preset);
    }

    // Custom names no parts, and applying it changes none.
    an::ui::applyThemePreset(ThemePreset::Nebula);
    an::ui::applyThemePreset(ThemePreset::Custom);
    EXPECT_EQ(an::ui::themePreset(), ThemePreset::Nebula);
}

// A part changed by hand leaves the preset: the accent, the tint or the strength alone makes it Custom.
TEST(Presets, AHandChangeMakesItCustom)
{
    const PartsRestored restored;
    an::ui::applyThemePreset(ThemePreset::Observatory);
    an::ui::setThemeAccentFamily(ThemeAccentFamily::Blue);
    EXPECT_EQ(an::ui::themePreset(), ThemePreset::Custom) << "the accent";

    an::ui::applyThemePreset(ThemePreset::Observatory);
    an::ui::setThemeSurfaceTintFamily(ThemeSurfaceTintFamily::Mint);
    EXPECT_EQ(an::ui::themePreset(), ThemePreset::Custom) << "the tint";

    an::ui::applyThemePreset(ThemePreset::Observatory);
    an::ui::setThemeTintStrength(ThemeTintStrength::Subtle);
    EXPECT_EQ(an::ui::themePreset(), ThemePreset::Custom) << "the strength";
}

// The settings of an earlier version hold an accent and a tint and no strength, which is then Balanced: they are the
// preset they make, or Custom.
TEST(Presets, EarlierSettingsMapToAPresetOrToCustom)
{
    const auto earlier = [](const ThemeAccentFamily accent, const ThemeSurfaceTintFamily tint) {
        return an::ui::themePresetOf(accent, tint, ThemeTintStrength::Balanced);
    };
    EXPECT_EQ(earlier(ThemeAccentFamily::Blue, ThemeSurfaceTintFamily::Neutral), ThemePreset::SlateBlue);
    EXPECT_EQ(earlier(ThemeAccentFamily::Amber, ThemeSurfaceTintFamily::Neutral), ThemePreset::Graphite);
    EXPECT_EQ(earlier(ThemeAccentFamily::Teal, ThemeSurfaceTintFamily::Ocean), ThemePreset::DeepSea);
    EXPECT_EQ(earlier(ThemeAccentFamily::Rose, ThemeSurfaceTintFamily::Amethyst), ThemePreset::Nebula);
    // The Observatory and Aurora presets need a strength an earlier version never stored.
    EXPECT_EQ(earlier(ThemeAccentFamily::Amber, ThemeSurfaceTintFamily::Ocean), ThemePreset::Custom);
    EXPECT_EQ(earlier(ThemeAccentFamily::Teal, ThemeSurfaceTintFamily::Mint), ThemePreset::Custom);
    EXPECT_EQ(earlier(ThemeAccentFamily::Blue, ThemeSurfaceTintFamily::Ocean), ThemePreset::Custom);
    EXPECT_EQ(earlier(ThemeAccentFamily::Amethyst, ThemeSurfaceTintFamily::Neutral), ThemePreset::Custom);
}

// The names a setting stores, read back in any case; anything else is Custom.
TEST(Presets, TheSettingsNames)
{
    const std::array<std::pair<ThemePreset, const char*>, 7u> names{{
        {ThemePreset::SlateBlue, "slate"},
        {ThemePreset::Observatory, "observatory"},
        {ThemePreset::DeepSea, "deepsea"},
        {ThemePreset::Graphite, "graphite"},
        {ThemePreset::Nebula, "nebula"},
        {ThemePreset::Aurora, "aurora"},
        {ThemePreset::Custom, "custom"},
    }};
    for (const auto& [preset, name] : names)
    {
        EXPECT_EQ(an::ui::themePresetId(preset), QString::fromLatin1(name));
        EXPECT_EQ(an::ui::resolveThemePreset(QString::fromLatin1(name).toUpper()), preset) << name;
    }
    EXPECT_EQ(an::ui::resolveThemePreset(QStringLiteral("sunset")), ThemePreset::Custom);
    EXPECT_EQ(an::ui::resolveThemePreset(QString()), ThemePreset::Custom);
}
