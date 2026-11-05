// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QTableWidget>

#include "processes/ImageIntegration/tabs/RegistrationTab.h"
#include "processes/ImageIntegration/tabs/StackingTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{
void ensureApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

class NumericSortItem final : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::EditRole).toInt() < other.data(Qt::EditRole).toInt();
    }
};
} // namespace

TEST(RegistrationTab, SortsByStarsAscending)
{
    ensureApp();

    IntegrationDataModel model;
    RegistrationTab tab(&model);

    QTableWidget* table = tab.findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(table != nullptr);

    table->setRowCount(0);
    table->clearContents();
    table->clearSpans();
    table->setRowCount(2);
    auto* a = new NumericSortItem(QStringLiteral("10"));
    a->setData(Qt::EditRole, 10);
    table->setItem(0, 1, a);

    auto* b = new NumericSortItem(QStringLiteral("5"));
    b->setData(Qt::EditRole, 5);
    table->setItem(1, 1, b);

    tab.sortFrameQualityByColumn(1, Qt::AscendingOrder);

    EXPECT_EQ(table->item(0, 1)->data(Qt::EditRole).toInt(), 5);
    EXPECT_EQ(tab.frameQualitySortIndicatorColumn(), 1);
    EXPECT_EQ(tab.frameQualitySortIndicatorOrder(), Qt::AscendingOrder);
}

TEST(RegistrationTab, RemembersSortPreference)
{
    ensureApp();

    IntegrationDataModel model;
    {
        RegistrationTab tab(&model);
        tab.sortFrameQualityByColumn(2, Qt::DescendingOrder);
    }

    RegistrationTab tab2(&model);
    EXPECT_EQ(tab2.frameQualitySortIndicatorColumn(), 2);
    EXPECT_EQ(tab2.frameQualitySortIndicatorOrder(), Qt::DescendingOrder);
}

TEST(StackingTab, SortsByFramesAscending)
{
    ensureApp();

    IntegrationDataModel model;
    StackingTab tab(&model);

    QTableWidget* table = tab.findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);

    table->setRowCount(0);
    table->clearContents();
    table->clearSpans();
    table->setRowCount(2);
    auto* a = new NumericSortItem(QStringLiteral("10"));
    a->setData(Qt::EditRole, 10);
    table->setItem(0, 1, a);

    auto* b = new NumericSortItem(QStringLiteral("5"));
    b->setData(Qt::EditRole, 5);
    table->setItem(1, 1, b);

    tab.sortIntegrationGroupsByColumn(1, Qt::AscendingOrder);

    EXPECT_EQ(table->item(0, 1)->data(Qt::EditRole).toInt(), 5);
    EXPECT_EQ(tab.integrationGroupsSortIndicatorColumn(), 1);
    EXPECT_EQ(tab.integrationGroupsSortIndicatorOrder(), Qt::AscendingOrder);
}
