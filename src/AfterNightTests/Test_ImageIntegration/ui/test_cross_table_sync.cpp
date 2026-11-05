// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>

#include <optional>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

class CrossTableSyncTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
    }

    void TearDown() override
    {
        delete model;
    }

    IntegrationDataModel* model = nullptr;
};

TEST_F(CrossTableSyncTest, EmitsSelectionChangedOnFrameSelection)
{
    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    std::vector<FrameId> ids;
    ids.emplace_back("C:/test.fit");

    model->setSelectedFrameIds(ids);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(CrossTableSyncTest, EmitsSelectionChangedOnGroupSelection)
{
    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    std::vector<GroupKey> keys;
    keys.emplace_back(GroupKey{"group1"});

    model->setSelectedGroupKeys(keys);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(CrossTableSyncTest, EmitsSelectionChangedOnRegistrationGroupSelection)
{
    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    model->setSelectedRegistrationGroupKey(std::optional<GroupKey>{GroupKey{"reg_group1"}});

    EXPECT_EQ(spy.count(), 1);
    ASSERT_TRUE(model->selectedRegistrationGroupKey().has_value());
    EXPECT_EQ(model->selectedRegistrationGroupKey()->toString(), QStringLiteral("reg_group1"));
}

TEST_F(CrossTableSyncTest, NoSignalIfRegistrationSelectionIdentical)
{
    model->setSelectedRegistrationGroupKey(std::optional<GroupKey>{GroupKey{"reg_group1"}});

    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);
    model->setSelectedRegistrationGroupKey(std::optional<GroupKey>{GroupKey{"reg_group1"}});

    EXPECT_EQ(spy.count(), 0);
}

TEST_F(CrossTableSyncTest, NoSignalIfSelectionIdentical)
{
    std::vector<FrameId> ids;
    ids.emplace_back("C:/test.fit");
    model->setSelectedFrameIds(ids);

    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);
    model->setSelectedFrameIds(ids); // Same

    EXPECT_EQ(spy.count(), 0);
}

TEST_F(CrossTableSyncTest, SelectedFrameIdsReturnsCorrectValues)
{
    std::vector<FrameId> ids;
    ids.emplace_back("C:/file1.fit");
    ids.emplace_back("C:/file2.fit");
    ids.emplace_back("C:/file3.fit");

    model->setSelectedFrameIds(ids);

    const auto& retrieved = model->selectedFrameIds();
    ASSERT_EQ(retrieved.size(), 3);
    EXPECT_EQ(retrieved[0].canonicalPath(), "C:/file1.fit");
    EXPECT_EQ(retrieved[1].canonicalPath(), "C:/file2.fit");
    EXPECT_EQ(retrieved[2].canonicalPath(), "C:/file3.fit");
}

TEST_F(CrossTableSyncTest, SelectedGroupKeysReturnsCorrectValues)
{
    std::vector<GroupKey> keys;
    keys.emplace_back(GroupKey{"group1"});
    keys.emplace_back(GroupKey{"group2"});

    model->setSelectedGroupKeys(keys);

    const auto& retrieved = model->selectedGroupKeys();
    ASSERT_EQ(retrieved.size(), 2);
}

TEST_F(CrossTableSyncTest, ClearingSelectionEmitsSignal)
{
    std::vector<FrameId> ids;
    ids.emplace_back("C:/test.fit");
    model->setSelectedFrameIds(ids);

    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    // Clear selection
    model->setSelectedFrameIds({});

    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(model->selectedFrameIds().empty());
}

TEST_F(CrossTableSyncTest, MultipleSelectionsEmitMultipleSignals)
{
    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    std::vector<FrameId> ids1;
    ids1.emplace_back("C:/file1.fit");
    model->setSelectedFrameIds(ids1);

    std::vector<FrameId> ids2;
    ids2.emplace_back("C:/file2.fit");
    model->setSelectedFrameIds(ids2);

    std::vector<FrameId> ids3;
    ids3.emplace_back("C:/file3.fit");
    model->setSelectedFrameIds(ids3);

    EXPECT_EQ(spy.count(), 3);
}

TEST_F(CrossTableSyncTest, FrameAndGroupSelectionsAreIndependent)
{
    QSignalSpy spy(model, &IntegrationDataModel::selectionChanged);

    std::vector<FrameId> ids;
    ids.emplace_back("C:/file.fit");
    model->setSelectedFrameIds(ids);

    std::vector<GroupKey> keys;
    keys.emplace_back(GroupKey{"group"});
    model->setSelectedGroupKeys(keys);

    // Both should have emitted signals
    EXPECT_EQ(spy.count(), 2);

    // Both selections should be preserved
    EXPECT_EQ(model->selectedFrameIds().size(), 1);
    EXPECT_EQ(model->selectedGroupKeys().size(), 1);
}
