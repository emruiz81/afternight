// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCborMap>
#include <QString>

#include "HostThemeSync.h"

namespace
{

class ScopedThemePreferenceOverride final
{
public:
    ScopedThemePreferenceOverride() : original_preference_(an::ui::themePreference()) {}

    ~ScopedThemePreferenceOverride()
    {
        an::ui::setThemePreference(original_preference_);
        an::ui::refreshTokens();
    }

    void set(const an::ui::ThemePreference preference) const
    {
        an::ui::setThemePreference(preference);
    }

private:
    an::ui::ThemePreference original_preference_;
};

} // anonymous namespace

TEST(ExtensionHostThemeSyncTest, ResolveHostThemePreferencePrefersExplicitDarkFlag)
{
    const QCborMap tokens{{QStringLiteral("is_dark"), false}};

    const auto resolved = an::ui::extensions::resolveHostThemePreference("AfterNight Dark", tokens);

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, an::ui::ThemePreference::Light);
}

TEST(ExtensionHostThemeSyncTest, ResolveHostThemePreferenceFallsBackToThemeName)
{
    const auto dark_resolved = an::ui::extensions::resolveHostThemePreference("AfterNight Dark", QCborMap{});
    const auto light_resolved = an::ui::extensions::resolveHostThemePreference("AfterNight Light", QCborMap{});
    const auto missing_resolved = an::ui::extensions::resolveHostThemePreference("", QCborMap{});

    ASSERT_TRUE(dark_resolved.has_value());
    EXPECT_EQ(*dark_resolved, an::ui::ThemePreference::Dark);
    ASSERT_TRUE(light_resolved.has_value());
    EXPECT_EQ(*light_resolved, an::ui::ThemePreference::Light);
    EXPECT_FALSE(missing_resolved.has_value());
}

TEST(ExtensionHostThemeSyncTest, ApplyHostThemeSnapshotUpdatesThemePreference)
{
    ScopedThemePreferenceOverride preference_guard;
    preference_guard.set(an::ui::ThemePreference::Light);

    ASSERT_TRUE(
        an::ui::extensions::applyHostThemeSnapshot("AfterNight Dark", QCborMap{{QStringLiteral("is_dark"), true}}));
    EXPECT_EQ(an::ui::themePreference(), an::ui::ThemePreference::Dark);
    EXPECT_TRUE(an::ui::isDarkTheme());

    ASSERT_TRUE(
        an::ui::extensions::applyHostThemeSnapshot("AfterNight Light", QCborMap{{QStringLiteral("is_dark"), false}}));
    EXPECT_EQ(an::ui::themePreference(), an::ui::ThemePreference::Light);
    EXPECT_FALSE(an::ui::isDarkTheme());

    EXPECT_FALSE(an::ui::extensions::applyHostThemeSnapshot("", QCborMap{}));
    EXPECT_EQ(an::ui::themePreference(), an::ui::ThemePreference::Light);
}