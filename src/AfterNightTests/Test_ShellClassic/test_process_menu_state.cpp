// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <QMainWindow>

#include "AfterNightAppUiBuilder.h"
#include "app/ProcessMenuState.h"
#include "fixtures/RecordedActions.h"

namespace
{

using an::ui::RecentProcessEntryData;
using an::ui::process_registry::ProcessDescriptor;

const ProcessDescriptor* findDescriptor(const std::map<std::string, ProcessDescriptor>& descriptors,
                                        const std::string& id)
{
    const auto it = descriptors.find(id);
    return it == descriptors.end() ? nullptr : &it->second;
}

} // namespace

TEST(AfterNightAppProcessMenuSuite, DeserializeProcessIdSetParsesMixedSeparatorsAndWhitespace)
{
    const std::set<std::string> ids =
        an::ui::deserializeProcessIdSet(QStringLiteral(" calibrate ;stack\nintegrate,\r\n calibrate "));

    EXPECT_EQ(ids.size(), 3u);
    EXPECT_TRUE(ids.contains("calibrate"));
    EXPECT_TRUE(ids.contains("stack"));
    EXPECT_TRUE(ids.contains("integrate"));
}

TEST(AfterNightAppProcessMenuSuite, SerializeProcessIdSetSkipsEmptyAndUsesStableOrdering)
{
    std::set<std::string> ids = {"zeta", "", "alpha"};

    const QString serialized = an::ui::serializeProcessIdSet(ids);
    EXPECT_EQ(serialized, QStringLiteral("alpha\nzeta"));
}

TEST(AfterNightAppProcessMenuSuite, DeserializeOrderedProcessIdListPreservesFirstSeenOrder)
{
    const std::vector<std::string> ids = an::ui::deserializeOrderedProcessIdList(QStringLiteral("a,b;a\na\r\nc; b ,d"));

    ASSERT_EQ(ids.size(), 4u);
    EXPECT_EQ(ids[0], "a");
    EXPECT_EQ(ids[1], "b");
    EXPECT_EQ(ids[2], "c");
    EXPECT_EQ(ids[3], "d");
}

TEST(AfterNightAppProcessMenuSuite, SerializeOrderedProcessIdListPreservesOrderAndSkipsEmpty)
{
    const std::vector<std::string> ids = {"", "recent.first", "recent.second", "", "recent.third"};

    const QString serialized = an::ui::serializeOrderedProcessIdList(ids);
    EXPECT_EQ(serialized, QStringLiteral("recent.first\nrecent.second\nrecent.third"));
}

TEST(AfterNightAppProcessMenuSuite, ProcessDisplayNameFromDescriptorUsesExpectedFallbackChain)
{
    ProcessDescriptor descriptor;
    descriptor.id = "id.value";
    descriptor.action_object_name = "actionName";
    descriptor.process_name = "processName";
    descriptor.display_name = "displayName";
    EXPECT_EQ(an::ui::processDisplayNameFromDescriptor(&descriptor, "fallback"), QStringLiteral("displayName"));

    descriptor.display_name.clear();
    EXPECT_EQ(an::ui::processDisplayNameFromDescriptor(&descriptor, "fallback"), QStringLiteral("processName"));

    descriptor.process_name.clear();
    EXPECT_EQ(an::ui::processDisplayNameFromDescriptor(&descriptor, "fallback"), QStringLiteral("actionName"));

    descriptor.action_object_name.clear();
    EXPECT_EQ(an::ui::processDisplayNameFromDescriptor(&descriptor, "fallback"), QStringLiteral("id.value"));

    EXPECT_EQ(an::ui::processDisplayNameFromDescriptor(nullptr, "fallback"), QStringLiteral("fallback"));
}

TEST(AfterNightAppProcessMenuSuite, BuildRecentProcessEntriesPreservesUnknownDescriptors)
{
    std::map<std::string, ProcessDescriptor> descriptors;
    descriptors.emplace("process.a", ProcessDescriptor{"process.a", "Alpha", "test", "actionA", "", "Proc A"});
    descriptors.emplace("process.b", ProcessDescriptor{"process.b", "", "test", "actionB", "", "Proc B"});

    const std::vector<std::string> orderedIds = {"missing", "process.a", "process.b"};
    const std::deque<RecentProcessEntryData> recentEntries = an::ui::buildRecentProcessEntries(
        orderedIds, [&descriptors](const std::string& id) { return findDescriptor(descriptors, id); });

    ASSERT_EQ(recentEntries.size(), 3u);
    EXPECT_EQ(recentEntries[0].id, "missing");
    EXPECT_EQ(recentEntries[0].displayName, "missing");
    EXPECT_EQ(recentEntries[1].id, "process.a");
    EXPECT_EQ(recentEntries[1].displayName, "Alpha");
    EXPECT_EQ(recentEntries[2].id, "process.b");
    EXPECT_EQ(recentEntries[2].displayName, "Proc B");
}

TEST(AfterNightAppProcessMenuSuite, PruneMissingRecentProcessEntriesRemovesUnknownDescriptorsAfterDiscovery)
{
    std::map<std::string, ProcessDescriptor> descriptors;
    descriptors.emplace("process.a", ProcessDescriptor{"process.a", "Alpha", "test", "actionA", "", "Proc A"});
    descriptors.emplace("process.b", ProcessDescriptor{"process.b", "Beta", "test", "actionB", "", "Proc B"});

    std::deque<RecentProcessEntryData> recentEntries = {
        {"missing.extension", "missing.extension"}, {"process.a", "Alpha"}, {"process.b", "Beta"}};

    EXPECT_TRUE(an::ui::pruneMissingRecentProcessEntries(
        recentEntries, [&descriptors](const std::string& id) { return findDescriptor(descriptors, id); }));

    ASSERT_EQ(recentEntries.size(), 2u);
    EXPECT_EQ(recentEntries[0].id, "process.a");
    EXPECT_EQ(recentEntries[1].id, "process.b");

    EXPECT_FALSE(an::ui::pruneMissingRecentProcessEntries(
        recentEntries, [&descriptors](const std::string& id) { return findDescriptor(descriptors, id); }));
}

TEST(AfterNightAppProcessMenuSuite, UpdateRecentProcessListMaintainsMruAndConfiguredLimit)
{
    std::deque<RecentProcessEntryData> recentEntries = {{"process.old.1", "Old 1"}, {"process.old.2", "Old 2"}};

    EXPECT_TRUE(an::ui::updateRecentProcessList(recentEntries, "process.new", "New", 2));
    ASSERT_EQ(recentEntries.size(), 2u);
    EXPECT_EQ(recentEntries[0].id, "process.new");
    EXPECT_EQ(recentEntries[1].id, "process.old.1");

    EXPECT_TRUE(an::ui::updateRecentProcessList(recentEntries, "process.old.1", "Old 1 Updated", 2));
    ASSERT_EQ(recentEntries.size(), 2u);
    EXPECT_EQ(recentEntries[0].id, "process.old.1");
    EXPECT_EQ(recentEntries[0].displayName, "Old 1 Updated");
    EXPECT_EQ(recentEntries[1].id, "process.new");
}

TEST(AfterNightAppProcessMenuSuite, UpdateRecentProcessListRejectsEmptyIdAndUsesIdFallbackName)
{
    std::deque<RecentProcessEntryData> recentEntries;

    EXPECT_FALSE(an::ui::updateRecentProcessList(recentEntries, "", "ignored", 10));
    EXPECT_TRUE(recentEntries.empty());

    EXPECT_TRUE(an::ui::updateRecentProcessList(recentEntries, "process.fallback", "", 10));
    ASSERT_EQ(recentEntries.size(), 1u);
    EXPECT_EQ(recentEntries.front().displayName, "process.fallback");
}

TEST(AfterNightAppProcessMenuSuite, BuildFavoriteProcessEntriesFiltersMissingAndSortsCaseInsensitive)
{
    std::map<std::string, ProcessDescriptor> descriptors;
    descriptors.emplace("favorite.z", ProcessDescriptor{"favorite.z", "zeta", "test", "actionZ", "", "Zeta"});
    descriptors.emplace("favorite.a", ProcessDescriptor{"favorite.a", "Alpha", "test", "actionA", "", "Alpha"});
    descriptors.emplace("favorite.b", ProcessDescriptor{"favorite.b", "", "test", "actionB", "", "beta"});

    const std::set<std::string> favorites = {"favorite.z", "favorite.missing", "favorite.a", "favorite.b"};
    std::set<std::string> validFavoriteIds;
    const auto favoriteEntries = an::ui::buildFavoriteProcessEntries(
        favorites, [&descriptors](const std::string& id) { return findDescriptor(descriptors, id); },
        &validFavoriteIds);

    EXPECT_EQ(validFavoriteIds, std::set<std::string>({"favorite.a", "favorite.b", "favorite.z"}));
    ASSERT_EQ(favoriteEntries.size(), 3u);
    EXPECT_EQ(favoriteEntries[0].first, QStringLiteral("Alpha"));
    EXPECT_EQ(favoriteEntries[0].second, "favorite.a");
    EXPECT_EQ(favoriteEntries[1].first, QStringLiteral("beta"));
    EXPECT_EQ(favoriteEntries[1].second, "favorite.b");
    EXPECT_EQ(favoriteEntries[2].first, QStringLiteral("zeta"));
    EXPECT_EQ(favoriteEntries[2].second, "favorite.z");
}

TEST(AfterNightAppProcessMenuSuite, FavoriteMenuLayoutReflectsManageAndClearPlacementRules)
{
    const an::ui::FavoriteProcessMenuLayout emptyLayout = an::ui::buildFavoriteProcessMenuLayout(0);
    EXPECT_FALSE(emptyLayout.addSeparatorBeforeManage);
    EXPECT_FALSE(emptyLayout.clearEnabled);

    const an::ui::FavoriteProcessMenuLayout withFavorites = an::ui::buildFavoriteProcessMenuLayout(3);
    EXPECT_TRUE(withFavorites.addSeparatorBeforeManage);
    EXPECT_TRUE(withFavorites.clearEnabled);
}

TEST(AfterNightAppProcessMenuSuite, BuildUiProvidesDedicatedExtensionsMenuForManagement)
{
    QMainWindow window;
    const an::tests::fixtures::RecordedActions actions(window);
    ASSERT_TRUE(actions.loaded());
    an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window, actions.lookup());

    ASSERT_NE(ui.menuBar, nullptr);
    ASSERT_NE(ui.menuProcess, nullptr);
    ASSERT_NE(ui.menuExtensions, nullptr);
    ASSERT_NE(ui.menuWindow, nullptr);
    ASSERT_NE(ui.actionManage_Extensions, nullptr);

    const QList<QAction*> topLevelActions = ui.menuBar->actions();
    const int processIndex = topLevelActions.indexOf(ui.menuProcess->menuAction());
    const int extensionsIndex = topLevelActions.indexOf(ui.menuExtensions->menuAction());
    const int windowIndex = topLevelActions.indexOf(ui.menuWindow->menuAction());

    ASSERT_GE(processIndex, 0);
    ASSERT_GE(extensionsIndex, 0);
    ASSERT_GE(windowIndex, 0);
    EXPECT_LT(processIndex, extensionsIndex);
    EXPECT_LT(extensionsIndex, windowIndex);

    const QList<QAction*> extensionActions = ui.menuExtensions->actions();
    ASSERT_EQ(extensionActions.size(), 1);
    EXPECT_EQ(extensionActions.at(0), ui.actionManage_Extensions);
    EXPECT_TRUE(ui.menuExtensions->title().contains(QStringLiteral("Extensions")));
    EXPECT_TRUE(ui.actionManage_Extensions->text().contains(QStringLiteral("Manage")));
    EXPECT_TRUE(ui.actionManage_Extensions->toolTip().contains(QStringLiteral("configure")));
}

TEST(AfterNightAppProcessMenuSuite, BuildUiPlacesNBRGBCombinationNearLRGBInColorSpaces)
{
    QMainWindow window;
    const an::tests::fixtures::RecordedActions actions(window);
    ASSERT_TRUE(actions.loaded());
    an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window, actions.lookup());

    ASSERT_NE(ui.menuColor_Spaces, nullptr);
    ASSERT_NE(ui.actionLRGB_Combination, nullptr);
    ASSERT_NE(ui.actionNBRGB_Combination, nullptr);

    const QList<QAction*> colorActions = ui.menuColor_Spaces->actions();
    const int lrgbIndex = colorActions.indexOf(ui.actionLRGB_Combination);
    const int nbrgbIndex = colorActions.indexOf(ui.actionNBRGB_Combination);

    ASSERT_GE(lrgbIndex, 0);
    ASSERT_GE(nbrgbIndex, 0);
    EXPECT_EQ(nbrgbIndex, lrgbIndex + 1);
    EXPECT_TRUE(ui.actionNBRGB_Combination->text().contains(QStringLiteral("NBRGB Combination")));
}
