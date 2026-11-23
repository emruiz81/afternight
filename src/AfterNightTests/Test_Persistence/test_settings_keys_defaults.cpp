// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a fresh settings store means: every key the new interface added to the global settings store has its default,
/// which a reader takes while nothing is stored; and a store that holds nothing puts in force the choices AfterNight
/// 1.1 had - its theme, contrast, accent and tint, Night off - but for the shell, which is the Observatory one.

#include <gtest/gtest.h>

#include <QString>

#include "app/ThemeManager.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"
#include "theme/AnDesignTokens.h"

#include <optional>
#include <string>
#include <vector>

namespace
{

/// A key and the text a reader takes for it while nothing is stored.
struct KeyDefault
{
    const char* key;
    QString value;
};

QString switchText(const bool on)
{
    return on ? QStringLiteral("1") : QStringLiteral("0");
}

} // namespace

// Every key the new interface added to the global settings store has its default, the value its reader falls back to;
// none is stored in a fresh store. A store that holds nothing, read as the application reads it as it starts, gives
// the choices of 1.1: the dark theme, the balanced contrast, the blue accent, the neutral tint and Night off; and the
// preset a fresh store names is made of 1.1's accent and tint, so a store that keeps it puts nothing else in force. The
// shell is the one default that is not 1.1's: a fresh store starts the Observatory shell.
TEST(SettingsKeys, DefaultsReproduce1_1)
{
    const std::vector<KeyDefault> added = {
        {SettingsKeys::INTERFACE_SHELL, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)},
        {SettingsKeys::INTERFACE_THEME_TINT_STRENGTH,
         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH)},
        {SettingsKeys::INTERFACE_DENSITY, QString::fromLatin1(SettingsDefaults::INTERFACE_DENSITY)},
        {SettingsKeys::INTERFACE_THEME_PRESET, QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_PRESET)},
        {SettingsKeys::INTERFACE_START_IN_NIGHT, switchText(SettingsDefaults::INTERFACE_START_IN_NIGHT)},
        {SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR, switchText(SettingsDefaults::INTERFACE_ALWAYS_SHOW_MENU_BAR)},
        {SettingsKeys::INTERFACE_READOUT_COLLAPSED, switchText(SettingsDefaults::INTERFACE_READOUT_COLLAPSED)},
        {SettingsKeys::INTERFACE_READOUT_CARDS, QString::fromLatin1(SettingsDefaults::INTERFACE_READOUT_CARDS)},
        {SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS,
         QString::fromLatin1(SettingsDefaults::INTERFACE_PALETTE_RECENT_COMMANDS)},
        {SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN, switchText(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_OPEN)},
        {SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT,
         QString::number(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT)},
        {SettingsKeys::INTERFACE_CONSOLE_FLOATING, switchText(SettingsDefaults::INTERFACE_CONSOLE_FLOATING)},
        {SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT,
         QString::fromLatin1(SettingsDefaults::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT)},
        {SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS,
         switchText(SettingsDefaults::PROJECTS_CREATE_FOR_INTEGRATION_JOBS)},
        {SettingsKeys::HISTORY_COMPRESSION, QString::fromLatin1(SettingsDefaults::HISTORY_COMPRESSION)},
        {SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB,
         QString::number(SettingsDefaults::PROJECTS_COMPACT_ASK_THRESHOLD_GB)},
        {SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP, switchText(SettingsDefaults::PROJECTS_OPEN_LAST_ON_STARTUP)},
        {SettingsKeys::PROJECTS_RECENT, QString::fromLatin1(SettingsDefaults::PROJECTS_RECENT)},
        {SettingsKeys::PROJECTS_LAST, QString::fromLatin1(SettingsDefaults::PROJECTS_LAST)},
    };
    const std::vector<KeyDefault> stated = {
        {SettingsKeys::INTERFACE_SHELL, QStringLiteral("observatory")},
        {SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, QStringLiteral("balanced")},
        {SettingsKeys::INTERFACE_DENSITY, QStringLiteral("compact")},
        {SettingsKeys::INTERFACE_THEME_PRESET, QStringLiteral("slate")},
        {SettingsKeys::INTERFACE_START_IN_NIGHT, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_READOUT_COLLAPSED, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_READOUT_CARDS, QString()},
        {SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS, QString()},
        {SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT, QStringLiteral("220")},
        {SettingsKeys::INTERFACE_CONSOLE_FLOATING, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT, QStringLiteral("grid")},
        {SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS, QStringLiteral("1")},
        {SettingsKeys::HISTORY_COMPRESSION, QStringLiteral("lz4")},
        {SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB, QStringLiteral("10")},
        {SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP, QStringLiteral("0")},
        {SettingsKeys::PROJECTS_RECENT, QString()},
        {SettingsKeys::PROJECTS_LAST, QString()},
    };
    ASSERT_EQ(added.size(), stated.size());

    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    for (std::size_t index = 0; index < added.size(); ++index)
    {
        const QString key = QString::fromLatin1(added[index].key);
        EXPECT_EQ(QString::fromLatin1(stated[index].key), key);
        EXPECT_EQ(added[index].value, stated[index].value) << key.toStdString() << " has another default";
        EXPECT_EQ(store.selectOptional(key), std::nullopt) << key.toStdString() << " is stored in a fresh store";
        EXPECT_EQ(store.select(key, added[index].value), stated[index].value) << key.toStdString();
    }

    // The store read as the application reads it as it starts.
    const QString theme = store.select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME),
                                       QString::fromLatin1(SettingsDefaults::INTERFACE_THEME));
    const an::ui::ThemeChoices read{
        .theme = theme == QStringLiteral("light") ? an::ui::ThemeVariant::Light : an::ui::ThemeVariant::Dark,
        .contrast = an::ui::resolveThemeContrastProfile(
            store.select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE),
                         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE))),
        .accent = an::ui::resolveThemeAccentFamily(
            store.select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY),
                         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY))),
        .tint = an::ui::resolveThemeSurfaceTintFamily(
            store.select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY),
                         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY))),
        .tintStrength = an::ui::resolveThemeTintStrength(
            store.select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH),
                         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH))),
        .density = an::ui::resolveThemeDensity(store.select(QString::fromLatin1(SettingsKeys::INTERFACE_DENSITY),
                                                            QString::fromLatin1(SettingsDefaults::INTERFACE_DENSITY))),
        .night = store.select(QString::fromLatin1(SettingsKeys::INTERFACE_START_IN_NIGHT),
                              switchText(SettingsDefaults::INTERFACE_START_IN_NIGHT)) == QStringLiteral("1"),
    };
    // What 1.1 had: the dark theme, the balanced contrast, the blue accent, the neutral tint, and nothing else to say.
    const an::ui::ThemeChoices previous{.theme = an::ui::ThemeVariant::Dark,
                                        .contrast = an::ui::ThemeContrastProfile::Balanced,
                                        .accent = an::ui::ThemeAccentFamily::Blue,
                                        .tint = an::ui::ThemeSurfaceTintFamily::Neutral};
    EXPECT_EQ(read.theme, previous.theme);
    EXPECT_EQ(read.contrast, previous.contrast);
    EXPECT_EQ(read.accent, previous.accent);
    EXPECT_EQ(read.tint, previous.tint);
    EXPECT_FALSE(read.night) << "a fresh store starts in Night";
    EXPECT_TRUE(read == previous) << "a fresh store reads as other choices than 1.1's";
    EXPECT_EQ(store.select(QString::fromLatin1(SettingsKeys::INTERFACE_SHELL),
                           QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)),
              QStringLiteral("observatory"))
        << "a fresh store starts another shell than the Observatory one";

    // The preset a fresh store names is made of what 1.1 put in force.
    const std::optional<an::ui::ThemePresetParts> parts = an::ui::themePresetParts(
        an::ui::resolveThemePreset(QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_PRESET)));
    ASSERT_TRUE(parts.has_value());
    EXPECT_EQ(parts->accent, previous.accent);
    EXPECT_EQ(parts->tint, previous.tint);
    EXPECT_EQ(parts->strength, read.tintStrength);
}
