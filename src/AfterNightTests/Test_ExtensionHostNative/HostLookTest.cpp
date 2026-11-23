// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A full host follows the application's look: the theme state the application sends carries every choice in force -
/// the style profile, the tint's strength, the preset, the density and Night beside the theme, the contrast, the accent
/// and the tint - and the tokens they resolve to, the surface ladder and the sizes among them; a host given that state
/// resolves the very tokens the application resolved, an enumerator it does not know left as it was; a host is started
/// under the application's profile by an argument it reads back; and Night, sent to a host, reaches the window it
/// shows.

#include "controls/ImageViewControl.h"

#include "controls/ExtensionNativeProcessWindow.h"
#include "HostThemeSync.h"

#include "app/ExtensionThemeState.h"
#include "theme/AnDesignTokens.h"

#include <QCborArray>
#include <QCborMap>
#include <QColor>
#include <QLabel>
#include <QPalette>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{

using an::ui::StyleProfile;

/// The theme choices in force when a case starts, put back when it ends.
class HostLook : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_preference = an::ui::themePreference();
        m_contrast = an::ui::themeContrastProfile();
        m_accent = an::ui::themeAccentFamily();
        m_tint = an::ui::themeSurfaceTintFamily();
        m_strength = an::ui::themeTintStrength();
        m_density = an::ui::themeDensity();
        m_night = an::ui::nightModeSet();
    }

    void TearDown() override
    {
        an::ui::setNightMode(m_night);
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
        an::ui::setThemeTintStrength(m_strength);
        an::ui::setThemeDensity(m_density);
        an::ui::setThemePreference(m_preference);
        an::ui::refreshTokens();
    }

    /// The application's choices of the cases: the Observatory preset, Comfortable, the dark theme, high contrast.
    static void chooseAsTheApplication()
    {
        an::ui::applyThemePreset(an::ui::ThemePreset::Observatory);
        an::ui::setThemeDensity(an::ui::ThemeDensity::Comfortable);
        an::ui::setThemeContrastProfile(an::ui::ThemeContrastProfile::High);
        an::ui::setNightMode(false);
        an::ui::setThemePreference(an::ui::ThemePreference::Dark);
    }

    /// A host's choices before it is told the application's: every default, the light theme.
    static void chooseAsAFreshHost()
    {
        an::ui::applyThemePreset(an::ui::ThemePreset::SlateBlue);
        an::ui::setThemeDensity(an::ui::ThemeDensity::Compact);
        an::ui::setThemeContrastProfile(an::ui::ThemeContrastProfile::Balanced);
        an::ui::setNightMode(false);
        an::ui::setThemePreference(an::ui::ThemePreference::Light);
    }

private:
    an::ui::ThemePreference m_preference = an::ui::ThemePreference::System;
    an::ui::ThemeContrastProfile m_contrast = an::ui::ThemeContrastProfile::Balanced;
    an::ui::ThemeAccentFamily m_accent = an::ui::ThemeAccentFamily::Blue;
    an::ui::ThemeSurfaceTintFamily m_tint = an::ui::ThemeSurfaceTintFamily::Neutral;
    an::ui::ThemeTintStrength m_strength = an::ui::ThemeTintStrength::Balanced;
    an::ui::ThemeDensity m_density = an::ui::ThemeDensity::Compact;
    bool m_night = false;
};

/// The names of a map's entries, in its order.
[[nodiscard]] std::vector<QString> namesOf(const QCborMap& map)
{
    std::vector<QString> names;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        names.push_back(it.key().toString());
    }
    return names;
}

/// Whether a colour written as the state writes one holds no green and no blue.
[[nodiscard]] bool redOnly(const QCborValue& written)
{
    const QColor colour = QColor::fromString(written.toString());
    return colour.isValid() && colour.green() == 0 && colour.blue() == 0;
}

} // namespace

// The state names every choice and every resolved token group, in its order, and says the choices in force: the style
// profile by name, the strength, the preset, the density and Night by their numbers; the surface ladder and the sizes
// are the tokens in force.
TEST_F(HostLook, ThemeStateCarriesTheNewFields)
{
    chooseAsTheApplication();
    QCborMap state = an::ui::extensions::currentExtensionThemeTokens();

    EXPECT_EQ(namesOf(state), an::ui::extensions::extensionThemeStateNames());
    EXPECT_EQ(state.value(QStringLiteral("style_profile")).toString(), QStringLiteral("observatory"));
    EXPECT_EQ(state.value(QStringLiteral("tint_strength")).toInteger(),
              static_cast<int>(an::ui::ThemeTintStrength::Strong));
    EXPECT_EQ(state.value(QStringLiteral("preset")).toInteger(), static_cast<int>(an::ui::ThemePreset::Observatory));
    EXPECT_EQ(state.value(QStringLiteral("accent_family")).toInteger(),
              static_cast<int>(an::ui::ThemeAccentFamily::Amber));
    EXPECT_EQ(state.value(QStringLiteral("surface_tint_family")).toInteger(),
              static_cast<int>(an::ui::ThemeSurfaceTintFamily::Ocean));
    EXPECT_EQ(state.value(QStringLiteral("density")).toInteger(), static_cast<int>(an::ui::ThemeDensity::Comfortable));
    EXPECT_FALSE(state.value(QStringLiteral("night")).toBool());
    EXPECT_TRUE(state.value(QStringLiteral("is_dark")).toBool());

    const QCborMap layers = state.value(QStringLiteral("layers")).toMap();
    ASSERT_EQ(layers.size(), 5);
    EXPECT_EQ(layers.value(QStringLiteral("surface3")).toString(),
              an::ui::tokens().layers.surface3.name(QColor::HexArgb));
    const QCborMap metrics = state.value(QStringLiteral("metrics")).toMap();
    EXPECT_EQ(metrics.size(), 23);
    EXPECT_EQ(metrics.value(QStringLiteral("control")).toInteger(), an::ui::tokens().metrics.control);
    EXPECT_EQ(metrics.value(QStringLiteral("rowTable")).toInteger(), an::ui::tokens().metrics.rowTable);

    an::ui::setNightMode(true);
    state = an::ui::extensions::currentExtensionThemeTokens();
    EXPECT_TRUE(state.value(QStringLiteral("night")).toBool());

    const an::ui::extensions::ExtensionThemeSnapshot snapshot = an::ui::extensions::currentExtensionThemeSnapshot();
    EXPECT_EQ(snapshot.theme_name, "AfterNight Dark");
    EXPECT_EQ(snapshot.tokens, state);
}

// A host told the state resolves the tokens the application resolved, whatever it held before: the same colours, the
// same ladder, the same sizes and type. A choice the host does not know is left as it was. The host is started under
// the application's profile by `--style-profile`, whose name it reads back; "observatory" is the one name a profile
// has, and "classic" names none.
TEST_F(HostLook, HostResolvesTheApplicationsTokens)
{
    chooseAsTheApplication();
    const an::ui::DesignTokens application = an::ui::tokens();
    const an::ui::extensions::ExtensionThemeSnapshot snapshot = an::ui::extensions::currentExtensionThemeSnapshot();

    chooseAsAFreshHost();
    ASSERT_NE(an::ui::tokens(), application);
    ASSERT_TRUE(an::ui::extensions::applyHostThemeSnapshot(snapshot.theme_name, snapshot.tokens));
    EXPECT_EQ(an::ui::tokens(), application) << "the host resolves what the application resolved";
    EXPECT_EQ(an::ui::themeDensity(), an::ui::ThemeDensity::Comfortable);
    EXPECT_EQ(an::ui::themePreset(), an::ui::ThemePreset::Observatory);
    EXPECT_EQ(an::ui::extensions::currentExtensionThemeTokens(), snapshot.tokens)
        << "what the host answers an extension that paints itself is what the application sent";

    QCborMap unknown = snapshot.tokens;
    unknown.insert(QStringLiteral("density"), 7);
    unknown.insert(QStringLiteral("tint_strength"), -1);
    ASSERT_TRUE(an::ui::extensions::applyHostThemeSnapshot(snapshot.theme_name, unknown));
    EXPECT_EQ(an::ui::themeDensity(), an::ui::ThemeDensity::Comfortable) << "a density the host does not know";
    EXPECT_EQ(an::ui::themeTintStrength(), an::ui::ThemeTintStrength::Strong);

    EXPECT_EQ(an::ui::extensions::hostLookArguments(StyleProfile::Observatory),
              (std::vector<std::string>{"--style-profile", "observatory"}));
    EXPECT_EQ(an::ui::extensions::styleProfileName(StyleProfile::Observatory), QStringLiteral("observatory"));
    EXPECT_EQ(an::ui::extensions::styleProfileNamed(QStringLiteral(" Observatory ")),
              std::optional<StyleProfile>(StyleProfile::Observatory));
    EXPECT_FALSE(an::ui::extensions::styleProfileNamed(QStringLiteral("classic")).has_value());
    EXPECT_FALSE(an::ui::extensions::styleProfileNamed(QStringLiteral("workbench")).has_value());
}

// Night, sent to a host whose window is open, reaches the window: the host's tokens hold no green and no blue, and the
// text the window draws in them follows once the host refreshes it, as it does when the theme changes.
TEST_F(HostLook, NightReachesTheHostWindow)
{
    chooseAsTheApplication();
    an::ui::setNightMode(true);
    const an::ui::extensions::ExtensionThemeSnapshot snapshot = an::ui::extensions::currentExtensionThemeSnapshot();

    chooseAsAFreshHost();
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-night", nullptr);
    window.refreshTheme();
    ASSERT_FALSE(an::ui::isNightMode());
    // The text the window draws in the tokens' secondary colour, before Night.
    const QColor dayText = an::ui::tokens().colors.textSecondary;
    std::vector<const QLabel*> drawn;
    for (const QLabel* label : window.findChildren<QLabel*>())
    {
        if (label->palette().color(QPalette::WindowText) == dayText)
        {
            drawn.push_back(label);
        }
    }
    ASSERT_FALSE(drawn.empty()) << "the window draws text in the tokens";

    ASSERT_TRUE(an::ui::extensions::applyHostThemeSnapshot(snapshot.theme_name, snapshot.tokens));
    window.refreshTheme();
    EXPECT_TRUE(an::ui::isNightMode());
    const QCborMap state = an::ui::extensions::currentExtensionThemeTokens();
    for (const QString group : {QStringLiteral("colors"), QStringLiteral("layers")})
    {
        const QCborMap colours = state.value(group).toMap();
        ASSERT_FALSE(colours.isEmpty()) << group.toStdString();
        for (auto it = colours.cbegin(); it != colours.cend(); ++it)
        {
            EXPECT_TRUE(redOnly(it.value())) << group.toStdString() << "." << it.key().toString().toStdString()
                                             << " is " << it.value().toString().toStdString();
        }
    }
    for (const QLabel* label : drawn)
    {
        const QColor text = label->palette().color(QPalette::WindowText);
        EXPECT_EQ(text, an::ui::tokens().colors.textSecondary) << label->text().toStdString();
        EXPECT_EQ(text.green(), 0);
        EXPECT_EQ(text.blue(), 0);
    }
}
