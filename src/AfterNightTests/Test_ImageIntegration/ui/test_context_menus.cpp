// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QMenu>
#include <QTableWidget>

#include "processes/ImageIntegration/tabs/RegistrationTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

class ContextMenuTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        tab = new RegistrationTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    RegistrationTab* tab = nullptr;
};

TEST_F(ContextMenuTest, FrameTableHasContextMenuEnabled)
{
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);
    EXPECT_EQ(frameTable->contextMenuPolicy(), Qt::CustomContextMenu);
}

TEST_F(ContextMenuTest, GroupTableHasContextMenuEnabled)
{
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    EXPECT_EQ(groupTable->contextMenuPolicy(), Qt::CustomContextMenu);
}
