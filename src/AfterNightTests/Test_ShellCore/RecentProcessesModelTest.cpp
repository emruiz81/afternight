// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The processes launched last: the limit the settings give, the order a launch keeps, the names the registry gives,
/// and a list that is stored, read back, pruned and cleared.

#include "ShellCoreTestSupport.h"

#include "services/RecentProcessesModel.h"

#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::tests::shellcore::ProcessCatalogue;
using an::ui::shell::RecentProcessesModel;

/// The ids of the model's entries, most recent first.
[[nodiscard]] std::vector<std::string> idsOf(const RecentProcessesModel& model)
{
    std::vector<std::string> ids;
    for (const auto& entry : model.entries())
    {
        ids.push_back(entry.id);
    }
    return ids;
}

/// The ids of the model's entries with the names they are shown by.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> namedEntriesOf(const RecentProcessesModel& model)
{
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& entry : model.entries())
    {
        entries.emplace_back(entry.id, entry.displayName);
    }
    return entries;
}

[[nodiscard]] QString storedIds(MemorySettings& settings)
{
    return settings.select(QString::fromLatin1(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS));
}

using Ids = std::vector<std::string>;

class RecentProcessesModelTest : public ::testing::Test
{
protected:
    [[nodiscard]] RecentProcessesModel modelOverTheStore()
    {
        RecentProcessesModel model(m_catalogue.lookup());
        model.setSettings(&m_settings);
        return model;
    }

    ProcessCatalogue m_catalogue{
        {"curves", "Curves"}, {"levels", "Levels"}, {"hdr", "HDR Multiscale"}, {"blur", "Gaussian Blur"}};
    MemorySettings m_settings;
};

TEST_F(RecentProcessesModelTest, TheLimitIsTheSettingOrTenByDefault)
{
    RecentProcessesModel unstored(m_catalogue.lookup());
    EXPECT_EQ(unstored.limit(), 10u) << "with no settings store";

    RecentProcessesModel model = modelOverTheStore();
    EXPECT_EQ(model.limit(), 10u) << "with nothing stored";

    const QString key = QString::fromLatin1(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT);
    m_settings.upsert(key, QStringLiteral("3"));
    EXPECT_EQ(model.limit(), 3u);
    for (const QString& unusable : {QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("some")})
    {
        m_settings.upsert(key, unusable);
        EXPECT_EQ(model.limit(), 10u) << unusable.toStdString();
    }
}

TEST_F(RecentProcessesModelTest, ALaunchComesFirstOnceAndTheOldestLeavesPastTheLimit)
{
    m_settings.upsert(QString::fromLatin1(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT), QStringLiteral("3"));
    RecentProcessesModel model = modelOverTheStore();

    EXPECT_TRUE(model.record("curves", "Curves"));
    EXPECT_TRUE(model.record("levels", "Levels"));
    EXPECT_TRUE(model.record("hdr", "HDR Multiscale"));
    EXPECT_EQ(idsOf(model), (Ids{"hdr", "levels", "curves"}));

    EXPECT_TRUE(model.record("curves", "Curves"));
    EXPECT_EQ(idsOf(model), (Ids{"curves", "hdr", "levels"})) << "a second launch moves it to the front";

    EXPECT_TRUE(model.record("blur", "Gaussian Blur"));
    EXPECT_EQ(idsOf(model), (Ids{"blur", "curves", "hdr"})) << "the oldest leaves";
    EXPECT_EQ(storedIds(m_settings), QStringLiteral("blur\ncurves\nhdr")) << "every launch is stored";

    EXPECT_FALSE(model.record(std::string(), "Nothing")) << "a launch with no id is not recorded";
    // A launch with no name is shown by its id.
    EXPECT_TRUE(model.record("unnamed", std::string()));
    EXPECT_EQ(model.entries().front().displayName, "unnamed");
}

TEST_F(RecentProcessesModelTest, ReadsTheStoredOrderUnderTheNamesTheRegistryGives)
{
    m_settings.upsert(QString::fromLatin1(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS),
                      QStringLiteral("levels\ncurves,levels;gone"));
    RecentProcessesModel model = modelOverTheStore();

    model.load();

    using Named = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(namedEntriesOf(model), (Named{{"levels", "Levels"}, {"curves", "Curves"}, {"gone", "gone"}}))
        << "a repeated id is read once; one the registry does not hold is shown by its id until it is pruned";
}

TEST_F(RecentProcessesModelTest, ReadsBackTheListItStored)
{
    RecentProcessesModel model = modelOverTheStore();
    (void)model.record("blur", "Gaussian Blur");
    (void)model.record("hdr", "HDR Multiscale");

    RecentProcessesModel later = modelOverTheStore();
    later.load();

    EXPECT_EQ(namedEntriesOf(later), namedEntriesOf(model));
}

TEST_F(RecentProcessesModelTest, ReadingKeepsNoMoreThanTheLimit)
{
    m_settings.upsert(QString::fromLatin1(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS),
                      QStringLiteral("curves\nlevels\nhdr\nblur"));
    m_settings.upsert(QString::fromLatin1(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT), QStringLiteral("2"));
    RecentProcessesModel model = modelOverTheStore();

    model.load();
    model.enforceLimit();

    EXPECT_EQ(idsOf(model), (Ids{"curves", "levels"}));
}

// An extension that was removed leaves the list once every source of the registry has reported.
TEST_F(RecentProcessesModelTest, PruningDropsWhatTheRegistryNoLongerHolds)
{
    RecentProcessesModel model = modelOverTheStore();
    (void)model.record("curves", "Curves");
    (void)model.record("levels", "Levels");
    m_catalogue.remove("curves");

    EXPECT_TRUE(model.pruneMissing());
    EXPECT_EQ(idsOf(model), (Ids{"levels"}));
    EXPECT_FALSE(model.pruneMissing()) << "nothing more to prune";
}

TEST_F(RecentProcessesModelTest, ClearingEmptiesTheStoredList)
{
    RecentProcessesModel model = modelOverTheStore();
    (void)model.record("curves", "Curves");

    model.clear();

    EXPECT_TRUE(model.entries().empty());
    EXPECT_TRUE(storedIds(m_settings).isEmpty());
    RecentProcessesModel later = modelOverTheStore();
    later.load();
    EXPECT_TRUE(later.entries().empty());
}

TEST_F(RecentProcessesModelTest, WithNoStoreTheListReadsEmpty)
{
    RecentProcessesModel model(m_catalogue.lookup());
    (void)model.record("curves", "Curves");

    model.load();

    EXPECT_TRUE(model.entries().empty());
}

} // namespace
