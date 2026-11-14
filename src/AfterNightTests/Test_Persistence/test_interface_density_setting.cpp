// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that says how dense the Observatory style profile lays the interface out: its key, its default, and that
/// the global settings store keeps it.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceDensitySetting, IsStoredUnderInterfaceDensity)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_DENSITY, "interface/density");
}

TEST(InterfaceDensitySetting, DefaultsToCompact)
{
    EXPECT_STREQ(SettingsDefaults::INTERFACE_DENSITY, "compact");
}

TEST(InterfaceDensitySetting, TheGlobalSettingsStoreKeepsTheChoice)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_DENSITY);
    const QString fallback = QString::fromLatin1(SettingsDefaults::INTERFACE_DENSITY);

    // Never chosen: nothing is stored, and a reader falls back to the default.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("compact"));

    ASSERT_TRUE(store.upsert(key, QStringLiteral("comfortable")));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(QStringLiteral("comfortable")));
    EXPECT_EQ(store.select(key, fallback), QStringLiteral("comfortable"));
}
