// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that keeps the commands the command palette ran last: its key, that nothing is stored until a command
/// runs, and that the global settings store keeps the list, one id a line, as it was written.

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfacePaletteRecentCommandsSetting, IsStoredUnderInterfacePaletteRecentCommands)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS, "interface/paletteRecentCommands");
}

TEST(InterfacePaletteRecentCommandsSetting, TheGlobalSettingsStoreKeepsTheList)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS);

    // Nothing ran: nothing is stored, and a reader starts from an empty list.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
    EXPECT_TRUE(store.select(key).isEmpty());

    const QString ids =
        QStringList{QStringLiteral("process:tone_brightness.actionHistogram_Transformation"),
                    QStringLiteral("action:actionOpen"), QStringLiteral("settings:Interface/Appearance/Theme")}
            .join(QLatin1Char('\n'));
    ASSERT_TRUE(store.upsert(key, ids));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(ids));
}
