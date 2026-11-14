// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that names the shell the application starts with: its key, its default, and that the global
/// settings store keeps it.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceShellSetting, IsStoredUnderInterfaceShell)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_SHELL, "interface/shell");
}

TEST(InterfaceShellSetting, DefaultsToTheClassicShell)
{
    EXPECT_STREQ(SettingsDefaults::INTERFACE_SHELL, "classic");
}

TEST(InterfaceShellSetting, TheGlobalSettingsStoreKeepsTheChoice)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_SHELL);

    // Never chosen: nothing is stored, and a reader falls back to the default.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_EQ(store.select(key, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)), QStringLiteral("classic"));

    ASSERT_TRUE(store.upsert(key, QStringLiteral("observatory")));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(QStringLiteral("observatory")));
    EXPECT_EQ(store.select(key, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)), QStringLiteral("observatory"));
}
