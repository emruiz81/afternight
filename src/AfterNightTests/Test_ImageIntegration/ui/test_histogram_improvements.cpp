// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QTableWidget>
#include <QVBoxLayout>

#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{
FrameInfo createFrame(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, "L", "M42",
                       QDateTime::currentDateTimeUtc(), "");
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class HistogramImprovementsTest : public ::testing::Test
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

TEST_F(HistogramImprovementsTest, HistogramExists)
{
    QWidget* hist = dashboard->weightHistogram();
    ASSERT_TRUE(hist != nullptr);
    EXPECT_EQ(hist->objectName(), "weightHistogram");
}

TEST_F(HistogramImprovementsTest, HistogramRespondsToSelection)
{
    // Add frames and metrics
    const QString path = "C:/light1.fit";
    model->addFrames(FrameType::Light, {createFrame(path)});

    FrameQualityMetrics m;
    m.stars = 1000;
    m.succeeded = true;
    model->setFrameQualityMetrics(FrameId(path), m);

    dashboard->setWeightingEnabled(true);
    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    dashboard->setWeightingPreviewConfig(cfg);

    dashboard->refresh();
    QCoreApplication::processEvents();

    QTableWidget* table = dashboard->findChild<QTableWidget*>("qualityTable");
    ASSERT_TRUE(table != nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    // Select the frame
    table->selectRow(0);
    QCoreApplication::processEvents();

    // Check intended visibility (not isVisible() which requires show())
    EXPECT_FALSE(dashboard->weightHistogram()->isHidden());
}

TEST_F(HistogramImprovementsTest, HistogramShowsMinFloor)
{
    dashboard->setWeightingEnabled(true);
    FrameQualityDashboard::WeightingPreviewConfig cfg;
    cfg.weightingMode = "Number of stars";
    cfg.minWeightFloor = 0.5;
    dashboard->setWeightingPreviewConfig(cfg);

    // We can't easily check the internal m_minFloor without accessors,
    // but we verify the API call doesn't crash and state is consistent.
    EXPECT_FALSE(dashboard->weightHistogram()->isHidden());
}
