// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The favourite processes: a stored set, listed by the names the registry gives in their alphabetical order, pruned of
/// what the registry no longer holds, replaced and cleared. A set has no limit.

#include "ShellCoreTestSupport.h"

#include "services/FavoriteProcessesModel.h"

#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QString>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::tests::shellcore::ProcessCatalogue;
using an::ui::shell::FavoriteProcessesModel;
using Ids = std::set<std::string>;

class FavoriteProcessesModelTest : public ::testing::Test
{
protected:
    [[nodiscard]] FavoriteProcessesModel modelOverTheStore()
    {
        FavoriteProcessesModel model(m_catalogue.lookup());
        model.setSettings(&m_settings);
        return model;
    }

    [[nodiscard]] QString storedIds()
    {
        return m_settings.select(QString::fromLatin1(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS));
    }

    ProcessCatalogue m_catalogue{
        {"curves", "Curves"}, {"levels", "levels"}, {"hdr", "HDR Multiscale"}, {"blur", "Gaussian Blur"}};
    MemorySettings m_settings;
};

TEST_F(FavoriteProcessesModelTest, ReadsTheStoredSet)
{
    m_settings.upsert(QString::fromLatin1(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS),
                      QStringLiteral("hdr\r\ncurves, curves;levels"));
    FavoriteProcessesModel model = modelOverTheStore();

    model.load();

    EXPECT_EQ(model.ids(), (Ids{"curves", "hdr", "levels"}));
}

TEST_F(FavoriteProcessesModelTest, ListsTheFavouritesTheRegistryHoldsByTheirNamesInAlphabeticalOrder)
{
    FavoriteProcessesModel model = modelOverTheStore();
    model.setIds({"levels", "hdr", "curves", "gone"});

    using Entries = std::vector<std::pair<QString, std::string>>;
    EXPECT_EQ(model.entries(), (Entries{{QStringLiteral("Curves"), "curves"},
                                        {QStringLiteral("HDR Multiscale"), "hdr"},
                                        {QStringLiteral("levels"), "levels"}}))
        << "case does not order the names, and an id the registry does not hold is not listed";
}

TEST_F(FavoriteProcessesModelTest, ReadsBackTheSetItStored)
{
    FavoriteProcessesModel model = modelOverTheStore();
    model.setIds({"blur", "curves"});
    EXPECT_EQ(storedIds(), QStringLiteral("blur\ncurves"));

    FavoriteProcessesModel later = modelOverTheStore();
    later.load();

    EXPECT_EQ(later.ids(), (Ids{"blur", "curves"}));
}

TEST_F(FavoriteProcessesModelTest, PruningDropsWhatTheRegistryNoLongerHoldsAndStoresTheRest)
{
    FavoriteProcessesModel model = modelOverTheStore();
    model.setIds({"blur", "curves"});
    m_catalogue.remove("blur");

    EXPECT_TRUE(model.pruneMissing());
    EXPECT_EQ(model.ids(), (Ids{"curves"}));
    EXPECT_EQ(storedIds(), QStringLiteral("curves"));
    EXPECT_FALSE(model.pruneMissing()) << "nothing more to prune";
}

TEST_F(FavoriteProcessesModelTest, ClearingEmptiesTheStoredSet)
{
    FavoriteProcessesModel model = modelOverTheStore();
    model.setIds({"blur", "curves"});

    model.clear();

    EXPECT_TRUE(model.ids().empty());
    EXPECT_TRUE(storedIds().isEmpty());
    FavoriteProcessesModel later = modelOverTheStore();
    later.load();
    EXPECT_TRUE(later.ids().empty());
}

TEST_F(FavoriteProcessesModelTest, WithNoStoreTheSetReadsEmpty)
{
    FavoriteProcessesModel model(m_catalogue.lookup());
    model.setIds({"curves"});

    model.load();

    EXPECT_TRUE(model.ids().empty());
}

} // namespace
