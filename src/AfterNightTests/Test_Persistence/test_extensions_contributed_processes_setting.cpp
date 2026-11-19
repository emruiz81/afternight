// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The setting that keeps the processes an extension contributed: its key, one an extension under the application's
/// prefix, apart from the extension's own settings; that nothing is stored until an extension contributes; and that the
/// global settings store keeps the list's JSON as it was written, and forgets it when it is removed.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(ExtensionsContributedProcessesSetting, IsStoredUnderTheApplicationsPrefixAndTheExtensionsId)
{
    EXPECT_STREQ(SettingsKeys::EXTENSIONS_CONTRIBUTED_PROCESSES_PREFIX, "extensions/contributedProcesses/");
    // An extension's own settings are kept under "extension/<id>/": no key of its own reaches the application's.
    EXPECT_FALSE(QString::fromLatin1(SettingsKeys::EXTENSIONS_CONTRIBUTED_PROCESSES_PREFIX)
                     .startsWith(QStringLiteral("extension/")));
}

TEST(ExtensionsContributedProcessesSetting, TheGlobalSettingsStoreKeepsTheList)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString key =
        QString::fromLatin1(SettingsKeys::EXTENSIONS_CONTRIBUTED_PROCESSES_PREFIX) + QStringLiteral("an.demo.scripts");

    // Nothing contributed: nothing is stored.
    EXPECT_EQ(store.selectOptional(key), std::nullopt);

    const QString list = QStringLiteral(
        R"([{"display_name":"M42 Tone","group":"Nebulae","id_suffix":"m42"},{"display_name":"Star Fix","id_suffix":"stars"}])");
    ASSERT_TRUE(store.upsert(key, list));
    EXPECT_EQ(store.selectOptional(key), std::optional<QString>(list));
    EXPECT_EQ(store.selectKeysWithPrefix(QString::fromLatin1(SettingsKeys::EXTENSIONS_CONTRIBUTED_PROCESSES_PREFIX)),
              QStringList{key});

    ASSERT_TRUE(store.remove(key));
    EXPECT_EQ(store.selectOptional(key), std::nullopt);
}
