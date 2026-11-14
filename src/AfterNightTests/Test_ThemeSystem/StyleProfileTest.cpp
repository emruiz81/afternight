// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The style profile: Classic until a shell names another, put in force only before the first widget exists, the
// refusal logged; the tint strength, which the Observatory profile reads and the Classic one ignores; and the
// application's resolution, which is the pure resolver's answer for the choices in force.

#include "app/ThemeManager.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{

using an::ui::DesignTokens;
using an::ui::StyleProfile;
using an::ui::ThemeChoices;
using an::ui::ThemeContrastProfile;
using an::ui::ThemeTintStrength;
using an::ui::ThemeVariant;
using an::ui::testing::ScopedStyleProfile;

constexpr std::array<ThemeContrastProfile, 4u> kContrasts{ThemeContrastProfile::High, ThemeContrastProfile::Balanced,
                                                          ThemeContrastProfile::Soft, ThemeContrastProfile::Oled};
constexpr std::array<an::ui::ThemeAccentFamily, 5u> kAccents{
    an::ui::ThemeAccentFamily::Blue, an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
    an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose};
constexpr std::array<an::ui::ThemeSurfaceTintFamily, 6u> kTints{
    an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeSurfaceTintFamily::Ocean,
    an::ui::ThemeSurfaceTintFamily::Mint,    an::ui::ThemeSurfaceTintFamily::Amethyst,
    an::ui::ThemeSurfaceTintFamily::Rose,    an::ui::ThemeSurfaceTintFamily::Amber};
constexpr std::array<ThemeTintStrength, 4u> kStrengths{ThemeTintStrength::Off, ThemeTintStrength::Subtle,
                                                       ThemeTintStrength::Balanced, ThemeTintStrength::Strong};

/// The interface's choices as they were, put back when the case ends.
class ChoicesRestored final
{
public:
    ChoicesRestored()
        : m_backdrop(an::ui::materialBackdropMode()), m_contrast(an::ui::themeContrastProfile()),
          m_accent(an::ui::themeAccentFamily()), m_tint(an::ui::themeSurfaceTintFamily()),
          m_strength(an::ui::themeTintStrength())
    {
    }

    ~ChoicesRestored()
    {
        an::ui::setMaterialBackdropMode(m_backdrop);
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
        an::ui::setThemeTintStrength(m_strength);
    }

    ChoicesRestored(const ChoicesRestored&) = delete;
    ChoicesRestored& operator=(const ChoicesRestored&) = delete;

private:
    an::ui::MaterialBackdropMode m_backdrop;
    ThemeContrastProfile m_contrast;
    an::ui::ThemeAccentFamily m_accent;
    an::ui::ThemeSurfaceTintFamily m_tint;
    ThemeTintStrength m_strength;
};

/// Whether the log's recent records hold one that contains `text`.
bool loggedRecently(const std::string& text)
{
    for (const auto& record : an::platform::recentLogMessages(50))
    {
        if (std::string(record.payload.data(), record.payload.size()).find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

QString widgetNames()
{
    QStringList names;
    for (const QWidget* widget : QApplication::allWidgets())
    {
        names.append(QString::fromLatin1(widget->metaObject()->className()) + QLatin1Char(' ') + widget->objectName());
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

// Until a shell names another, the profile in force is Classic.
TEST(StyleProfile, ClassicIsInForceUntilAShellNamesAnother)
{
    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Classic);
}

// Before the first widget a profile is put in force and the tokens are resolved under it.
TEST(StyleProfile, BeforeTheFirstWidgetAProfileIsPutInForce)
{
    ASSERT_TRUE(QApplication::allWidgets().isEmpty()) << widgetNames().toStdString();
    const ChoicesRestored restored;
    const ScopedStyleProfile classicAfterwards(StyleProfile::Classic);
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);

    EXPECT_TRUE(an::ui::setStyleProfile(StyleProfile::Observatory));

    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Observatory);
    ThemeChoices choices{.contrast = an::ui::themeContrastProfile(),
                         .accent = an::ui::themeAccentFamily(),
                         .tint = an::ui::themeSurfaceTintFamily(),
                         .tintStrength = an::ui::themeTintStrength(),
                         .profile = StyleProfile::Observatory};
    EXPECT_TRUE(an::ui::resolvedDarkTokens() == an::ui::resolveDesignTokens(choices));
}

// Once a widget exists a widget is sized and painted under the profile in force: another is refused, the refusal is
// logged, and nothing changes. The profile in force is still accepted.
TEST(StyleProfile, IsRefusedOnceAWidgetExistsAndTheRefusalIsLogged)
{
    ASSERT_TRUE(an::platform::isLoggingInitialized()) << "the suite logs to its ring buffer";
    ASSERT_EQ(an::ui::styleProfile(), StyleProfile::Classic);
    const DesignTokens before = an::ui::tokens();
    const QWidget widget;

    EXPECT_FALSE(an::ui::setStyleProfile(StyleProfile::Observatory));

    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Classic);
    EXPECT_TRUE(an::ui::tokens() == before) << "a refused profile resolved the tokens again";
    EXPECT_TRUE(loggedRecently("The observatory style profile is refused"));
    EXPECT_TRUE(an::ui::setStyleProfile(StyleProfile::Classic));
}

// The Classic profile has one blend: the tint strength moves none of its tokens, in any combination.
TEST(StyleProfile, ClassicIgnoresTheTintStrength)
{
    for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
    {
        for (const ThemeContrastProfile contrast : kContrasts)
        {
            for (const an::ui::ThemeSurfaceTintFamily tint : kTints)
            {
                ThemeChoices choices{.theme = theme, .contrast = contrast, .tint = tint};
                const DesignTokens balanced = an::ui::resolveDesignTokens(choices);
                for (const ThemeTintStrength strength : kStrengths)
                {
                    choices.tintStrength = strength;
                    EXPECT_TRUE(an::ui::resolveDesignTokens(choices) == balanced)
                        << "theme " << static_cast<int>(theme) << ", contrast " << static_cast<int>(contrast)
                        << ", tint " << static_cast<int>(tint) << ", strength " << static_cast<int>(strength);
                }
            }
        }
    }

    const ChoicesRestored restored;
    an::ui::setThemeSurfaceTintFamily(an::ui::ThemeSurfaceTintFamily::Ocean);
    const DesignTokens before = an::ui::tokens();
    an::ui::setThemeTintStrength(ThemeTintStrength::Strong);
    EXPECT_TRUE(an::ui::tokens() == before);
}

// The tokens in force are the pure resolver's answer for the choices in force, under either profile; under the
// Observatory profile the tint strength is one of them.
TEST(StyleProfile, TheTokensInForceAreTheResolversAnswer)
{
    const ChoicesRestored restored;
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);
    an::ui::setThemeContrastProfile(ThemeContrastProfile::Soft);
    an::ui::setThemeAccentFamily(an::ui::ThemeAccentFamily::Amber);
    an::ui::setThemeSurfaceTintFamily(an::ui::ThemeSurfaceTintFamily::Ocean);
    an::ui::setThemeTintStrength(ThemeTintStrength::Strong);

    for (const StyleProfile profile : {StyleProfile::Classic, StyleProfile::Observatory})
    {
        const ScopedStyleProfile inForce(profile);
        for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
        {
            const ThemeChoices choices{.theme = theme,
                                       .contrast = ThemeContrastProfile::Soft,
                                       .accent = an::ui::ThemeAccentFamily::Amber,
                                       .tint = an::ui::ThemeSurfaceTintFamily::Ocean,
                                       .tintStrength = ThemeTintStrength::Strong,
                                       .profile = profile};
            const DesignTokens& resolved =
                theme == ThemeVariant::Dark ? an::ui::resolvedDarkTokens() : an::ui::resolvedLightTokens();
            EXPECT_TRUE(resolved == an::ui::resolveDesignTokens(choices))
                << "profile " << static_cast<int>(profile) << ", theme " << static_cast<int>(theme);
        }
    }

    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    const DesignTokens strong = an::ui::resolvedDarkTokens();
    an::ui::setThemeTintStrength(ThemeTintStrength::Off);
    EXPECT_FALSE(an::ui::resolvedDarkTokens() == strong) << "the strength moved no Observatory token";
}

// The setting's values name the four strengths, and anything else is the default, Balanced.
TEST(TintStrengthSetting, EachStrengthIsStoredByItsName)
{
    for (const ThemeTintStrength strength : kStrengths)
    {
        EXPECT_EQ(an::ui::resolveThemeTintStrength(an::ui::themeTintStrengthId(strength)), strength);
    }
    EXPECT_EQ(an::ui::themeTintStrengthId(ThemeTintStrength::Off), QStringLiteral("off"));
    EXPECT_EQ(an::ui::themeTintStrengthId(ThemeTintStrength::Subtle), QStringLiteral("subtle"));
    EXPECT_EQ(an::ui::themeTintStrengthId(ThemeTintStrength::Balanced), QStringLiteral("balanced"));
    EXPECT_EQ(an::ui::themeTintStrengthId(ThemeTintStrength::Strong), QStringLiteral("strong"));
    EXPECT_EQ(an::ui::resolveThemeTintStrength(QStringLiteral(" Strong ")), ThemeTintStrength::Strong);
    EXPECT_EQ(an::ui::resolveThemeTintStrength(QStringLiteral("vivid")), ThemeTintStrength::Balanced);
    EXPECT_EQ(an::ui::resolveThemeTintStrength(QString()), ThemeTintStrength::Balanced);
}
