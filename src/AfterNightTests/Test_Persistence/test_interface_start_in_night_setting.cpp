// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that starts the application in Night: its key, its default - off, Night being never kept with the other
/// choices - and that the global settings store keeps it.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceStartInNightSetting, IsStoredUnderInterfaceStartInNight)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_START_IN_NIGHT, "interface/startInNight");
}

TEST(InterfaceStartInNightSetting, DefaultsToOff)
{
    EXPECT_FALSE(SettingsDefaults::INTERFACE_START_IN_NIGHT);
}

TEST(InterfaceStartInNightSetting, TheGlobalSettingsStoreKeepsTheChoice)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_START_IN_NIGHT);
    const QString fallback = SettingsDefaults::INTERFACE_START_IN_NIGHT ? QStringLiteral("1") : QStringLiteral("0");

    // Never chosen: nothing is stored, and a reader falls back to the default.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("0"));

    ASSERT_TRUE(store.upsert(key, QStringLiteral("1")));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(QStringLiteral("1")));
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("1"));
}
