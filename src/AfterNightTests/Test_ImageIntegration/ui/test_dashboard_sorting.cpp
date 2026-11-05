// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QTableWidget>
#include <QHeaderView>

#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

// Re-using createFrame from test_quality_dashboard.cpp logic
static FrameInfo createTestFrame(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000),
                       1.0, // exposure
                       std::nullopt, // temp
                       std::nullopt, // gain
                       std::nullopt, // offset
                       "L", // filter
                       "Obj", // object
                       QDateTime::currentDateTimeUtc(),
                       "" // readout mode
    );
    return FrameInfo(FrameId(path), path, meta);
}

class DashboardSortingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        dashboard = new FrameQualityDashboard(model);
    }

    void TearDown() override
    {
        delete dashboard;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    FrameQualityDashboard* dashboard = nullptr;
};

TEST_F(DashboardSortingTest, NumericSortingWorks)
{
    // Add 3 frames with different star counts
    QString p1 = "C:/frame1.fit"; // 100 stars
    QString p2 = "C:/frame2.fit"; // 500 stars
    QString p3 = "C:/frame3.fit"; // 1000 stars

    model->addFrames(FrameType::Light, {createTestFrame(p1), createTestFrame(p2), createTestFrame(p3)});

    FrameQualityMetrics m1;
    m1.stars = 100;
    m1.succeeded = true;
    FrameQualityMetrics m2;
    m2.stars = 500;
    m2.succeeded = true;
    FrameQualityMetrics m3;
    m3.stars = 1000;
    m3.succeeded = true;

    model->setFrameQualityMetrics(FrameId(p1), m1);
    model->setFrameQualityMetrics(FrameId(p2), m2);
    model->setFrameQualityMetrics(FrameId(p3), m3);

    dashboard->refresh();

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);

    // Sort by Stars column (index 2) Ascending
    table->horizontalHeader()->setSortIndicator(2, Qt::AscendingOrder);

    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toString(), FrameId(p1).canonicalPath());
    EXPECT_EQ(table->item(1, 0)->data(Qt::UserRole).toString(), FrameId(p2).canonicalPath());
    EXPECT_EQ(table->item(2, 0)->data(Qt::UserRole).toString(), FrameId(p3).canonicalPath());

    // Sort Descending
    table->horizontalHeader()->setSortIndicator(2, Qt::DescendingOrder);

    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toString(), FrameId(p3).canonicalPath());
    EXPECT_EQ(table->item(1, 0)->data(Qt::UserRole).toString(), FrameId(p2).canonicalPath());
    EXPECT_EQ(table->item(2, 0)->data(Qt::UserRole).toString(), FrameId(p1).canonicalPath());
}

TEST_F(DashboardSortingTest, NumericSortingHandlesDecimalValues)
{
    QString p1 = "C:/f1.fit"; // FWHM 1.5
    QString p2 = "C:/f2.fit"; // FWHM 2.5
    QString p3 = "C:/f3.fit"; // FWHM 10.0 (Lexicographically "10.0" < "2.5", but numerically not)

    model->addFrames(FrameType::Light, {createTestFrame(p1), createTestFrame(p2), createTestFrame(p3)});

    FrameQualityMetrics m1;
    m1.fwhm = 1.5;
    m1.succeeded = true;
    FrameQualityMetrics m2;
    m2.fwhm = 2.5;
    m2.succeeded = true;
    FrameQualityMetrics m3;
    m3.fwhm = 10.0;
    m3.succeeded = true;

    model->setFrameQualityMetrics(FrameId(p1), m1);
    model->setFrameQualityMetrics(FrameId(p2), m2);
    model->setFrameQualityMetrics(FrameId(p3), m3);

    dashboard->refresh();

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");

    // Sort by FWHM column (index 3) Ascending
    table->horizontalHeader()->setSortIndicator(3, Qt::AscendingOrder);

    // If it's numeric sorting, order should be f1, f2, f3
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toString(), FrameId(p1).canonicalPath());
    EXPECT_EQ(table->item(1, 0)->data(Qt::UserRole).toString(), FrameId(p2).canonicalPath());
    EXPECT_EQ(table->item(2, 0)->data(Qt::UserRole).toString(), FrameId(p3).canonicalPath());
}

TEST_F(DashboardSortingTest, SortingPersistsAcrossRefreshes)
{
    QString p1 = "C:/f1.fit";
    QString p2 = "C:/f2.fit";
    model->addFrames(FrameType::Light, {createTestFrame(p1), createTestFrame(p2)});

    FrameQualityMetrics m1;
    m1.stars = 100;
    m1.succeeded = true;
    FrameQualityMetrics m2;
    m2.stars = 500;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId(p1), m1);
    model->setFrameQualityMetrics(FrameId(p2), m2);

    dashboard->refresh();
    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");

    // Sort Descending by Stars
    table->horizontalHeader()->setSortIndicator(2, Qt::DescendingOrder);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toString(), FrameId(p2).canonicalPath());

    // Re-trigger refresh (simulating frames being added/removed)
    dashboard->refresh();

    // Should still be sorted descending
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toString(), FrameId(p2).canonicalPath());
    EXPECT_EQ(table->horizontalHeader()->sortIndicatorSection(), 2);
    EXPECT_EQ(table->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
}
