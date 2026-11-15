// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings that keep the readout column's state: their keys, their defaults - no card folded, the column open -
/// that nothing is stored until the person folds a card or collapses the column, and that the global settings store
/// keeps what was written.

#include <gtest/gtest.h>

#include <QString>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/InMemoryDatabase.h"

#include <optional>

TEST(InterfaceReadoutSettings, AreStoredUnderTheirKeysWithTheirDefaults)
{
    EXPECT_STREQ(SettingsKeys::INTERFACE_READOUT_CARDS, "interface/readoutCards");
    EXPECT_STREQ(SettingsKeys::INTERFACE_READOUT_COLLAPSED, "interface/readoutCollapsed");
    EXPECT_STREQ(SettingsDefaults::INTERFACE_READOUT_CARDS, "");
    EXPECT_FALSE(SettingsDefaults::INTERFACE_READOUT_COLLAPSED);
}

TEST(InterfaceReadoutSettings, TheGlobalSettingsStoreKeepsThem)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence store(db.executor());
    ASSERT_TRUE(store.initializeTable());
    const QString cards = QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_CARDS);
    const QString collapsed = QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_COLLAPSED);

    // Nothing was folded: nothing is stored, and the column decides by the window's height.
    EXPECT_EQ(store.selectOptional(cards), std::nullopt);
    EXPECT_EQ(store.selectOptional(collapsed), std::nullopt);

    ASSERT_TRUE(store.upsert(cards, QStringLiteral("statistics,image")));
    ASSERT_TRUE(store.upsert(collapsed, QStringLiteral("true")));
    EXPECT_EQ(store.selectOptional(cards), std::optional<QString>(QStringLiteral("statistics,image")));
    EXPECT_EQ(store.selectOptional(collapsed), std::optional<QString>(QStringLiteral("true")));
    // A column with every card open stores an empty line - not a null one, which the store would keep as no value -
    // and an empty line is remembered as such.
    ASSERT_TRUE(store.upsert(cards, QStringLiteral("")));
    EXPECT_EQ(store.selectOptional(cards), std::optional<QString>(QString()));
}
