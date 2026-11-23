// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The style profile: the Observatory one, in force from the start and still accepted once a widget exists; the tint
// strength, one of the choices it reads; and the application's resolution, which is the pure resolver's answer for the
// choices in force.

#include "app/ThemeManager.h"
#include "theme/AnDesignTokens.h"

#include <QApplication>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>

namespace
{

using an::ui::DesignTokens;
using an::ui::StyleProfile;
using an::ui::ThemeChoices;
using an::ui::ThemeContrastProfile;
using an::ui::ThemeTintStrength;
using an::ui::ThemeVariant;

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

// No shell has to name a profile: the Observatory one is in force from the start.
TEST(StyleProfile, ObservatoryIsInForceFromTheStart)
{
    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Observatory);
}

// Before the first widget a profile is put in force and the tokens are resolved under it.
TEST(StyleProfile, BeforeTheFirstWidgetAProfileIsPutInForce)
{
    ASSERT_TRUE(QApplication::allWidgets().isEmpty()) << widgetNames().toStdString();
    const ChoicesRestored restored;
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);

    EXPECT_TRUE(an::ui::setStyleProfile(StyleProfile::Observatory));

    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Observatory);
    ThemeChoices choices{.contrast = an::ui::themeContrastProfile(),
                         .accent = an::ui::themeAccentFamily(),
                         .tint = an::ui::themeSurfaceTintFamily(),
                         .tintStrength = an::ui::themeTintStrength()};
    EXPECT_TRUE(an::ui::resolvedDarkTokens() == an::ui::resolveDesignTokens(choices));
}

// Once a widget exists the profile in force is still accepted, and nothing changes.
TEST(StyleProfile, TheProfileInForceIsAcceptedOnceAWidgetExists)
{
    ASSERT_EQ(an::ui::styleProfile(), StyleProfile::Observatory);
    const DesignTokens before = an::ui::tokens();
    const QWidget widget;

    EXPECT_TRUE(an::ui::setStyleProfile(StyleProfile::Observatory));

    EXPECT_EQ(an::ui::styleProfile(), StyleProfile::Observatory);
    EXPECT_TRUE(an::ui::tokens() == before) << "the profile in force resolved the tokens again";
}

// The tokens in force are the pure resolver's answer for the choices in force, the tint strength among them.
TEST(StyleProfile, TheTokensInForceAreTheResolversAnswer)
{
    const ChoicesRestored restored;
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);
    an::ui::setThemeContrastProfile(ThemeContrastProfile::Soft);
    an::ui::setThemeAccentFamily(an::ui::ThemeAccentFamily::Amber);
    an::ui::setThemeSurfaceTintFamily(an::ui::ThemeSurfaceTintFamily::Ocean);
    an::ui::setThemeTintStrength(ThemeTintStrength::Strong);

    for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
    {
        const ThemeChoices choices{.theme = theme,
                                   .contrast = ThemeContrastProfile::Soft,
                                   .accent = an::ui::ThemeAccentFamily::Amber,
                                   .tint = an::ui::ThemeSurfaceTintFamily::Ocean,
                                   .tintStrength = ThemeTintStrength::Strong};
        const DesignTokens& resolved =
            theme == ThemeVariant::Dark ? an::ui::resolvedDarkTokens() : an::ui::resolvedLightTokens();
        EXPECT_TRUE(resolved == an::ui::resolveDesignTokens(choices)) << "theme " << static_cast<int>(theme);
    }

    const DesignTokens strong = an::ui::resolvedDarkTokens();
    an::ui::setThemeTintStrength(ThemeTintStrength::Off);
    EXPECT_FALSE(an::ui::resolvedDarkTokens() == strong) << "the strength moved no token";
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
