// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that names the colour preset: its key, its default, and that the global settings store keeps it.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfacePresetSetting, IsStoredUnderInterfaceThemePreset)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_THEME_PRESET, "interface/themePreset");
}

TEST(InterfacePresetSetting, DefaultsToSlateBlue)
{
    EXPECT_STREQ(SettingsDefaults::INTERFACE_THEME_PRESET, "slate");
}

TEST(InterfacePresetSetting, TheGlobalSettingsStoreKeepsTheChoice)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_THEME_PRESET);
    const QString fallback = QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_PRESET);

    // Never chosen: nothing is stored, and a reader falls back to the default.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("slate"));

    ASSERT_TRUE(store.upsert(key, QStringLiteral("nebula")));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(QStringLiteral("nebula")));
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("nebula"));
}
