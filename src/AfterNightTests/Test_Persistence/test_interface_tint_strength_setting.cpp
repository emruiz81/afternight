// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that says how much of the surface tint the Observatory style profile blends in: its key, its default,
/// and that the global settings store keeps it.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceTintStrengthSetting, IsStoredUnderInterfaceThemeTintStrength)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, "interface/themeTintStrength");
}

TEST(InterfaceTintStrengthSetting, DefaultsToBalanced)
{
    EXPECT_STREQ(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH, "balanced");
}

TEST(InterfaceTintStrengthSetting, TheGlobalSettingsStoreKeepsTheChoice)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH);
    const QString fallback = QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH);

    // Never chosen: nothing is stored, and a reader falls back to the default.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("balanced"));

    ASSERT_TRUE(store.upsert(key, QStringLiteral("strong")));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(QStringLiteral("strong")));
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("strong"));
}
