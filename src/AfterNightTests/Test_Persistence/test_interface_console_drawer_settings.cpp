// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings a shell keeps of its Session Console drawer from one start to the next: whether it is open, how high
/// it is and whether the console is in a window of its own - their keys, their defaults (closed, 220 px, attached) and
/// that the global settings store keeps them.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceConsoleDrawerSettings, AreStoredUnderTheirInterfaceKeys)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN, "interface/consoleDrawerOpen");
    EXPECT_STREQ(SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT, "interface/consoleDrawerHeight");
    EXPECT_STREQ(SettingsKeys::INTERFACE_CONSOLE_FLOATING, "interface/consoleFloating");
}

TEST(InterfaceConsoleDrawerSettings, DefaultToClosedAttachedAndTwoHundredTwentyPixelsHigh)
{
    EXPECT_FALSE(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_OPEN);
    EXPECT_EQ(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT, 220);
    EXPECT_FALSE(SettingsDefaults::INTERFACE_CONSOLE_FLOATING);
}

TEST(InterfaceConsoleDrawerSettings, TheGlobalSettingsStoreKeepsThem)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString open = QString::fromLatin1(SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN);
    const QString height = QString::fromLatin1(SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT);
    const QString floating = QString::fromLatin1(SettingsKeys::INTERFACE_CONSOLE_FLOATING);

    // Never chosen: nothing is stored, and a reader falls back to the defaults.
    EXPECT_EQ(store.selectOptional(open), std::nullopt);
    EXPECT_EQ(store.selectOptional(height), std::nullopt);
    EXPECT_EQ(store.selectOptional(floating), std::nullopt);
    EXPECT_EQ(store.select(height, QString::number(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT)),
              QStringLiteral("220"));

    ASSERT_TRUE(store.upsert(open, QStringLiteral("1")));
    ASSERT_TRUE(store.upsert(height, QStringLiteral("312")));
    ASSERT_TRUE(store.upsert(floating, QStringLiteral("1")));
    EXPECT_EQ(store.selectOptional(open), std::optional<QString>(QStringLiteral("1")));
    EXPECT_EQ(store.selectOptional(height), std::optional<QString>(QStringLiteral("312")));
    EXPECT_EQ(store.selectOptional(floating), std::optional<QString>(QStringLiteral("1")));
}
