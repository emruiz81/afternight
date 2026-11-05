// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPointingDevice>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include "processes/ImageIntegration/widgets/QualityDistributionWidget.h"

// The suite's harness provides the headless application.
class QualityDistributionWidgetTest : public ::testing::Test
{
};

using namespace ImageIntegration;

TEST_F(QualityDistributionWidgetTest, EmptyData)
{
    QualityDistributionWidget widget;
    EXPECT_FALSE(widget.hasData());
    widget.clearData();
    EXPECT_FALSE(widget.hasData());
}

TEST_F(QualityDistributionWidgetTest, SetData)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "/path/to/1";
    p1.score = 10.0;
    points.append(p1);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);
    EXPECT_TRUE(widget.hasData());
}

TEST_F(QualityDistributionWidgetTest, MetricSelection)
{
    QualityDistributionWidget widget;
    widget.setMetric(QualityMetric::FWHM);
    EXPECT_EQ(widget.metric(), QualityMetric::FWHM);

    widget.setMetric(QualityMetric::Stars);
    EXPECT_EQ(widget.metric(), QualityMetric::Stars);
}

TEST_F(QualityDistributionWidgetTest, SortModeSelection)
{
    QualityDistributionWidget widget;
    widget.setSortMode(SortMode::ByValueAsc);
    EXPECT_EQ(widget.sortMode(), SortMode::ByValueAsc);
}

TEST_F(QualityDistributionWidgetTest, PlotStyleSelection)
{
    QualityDistributionWidget widget;
    widget.setPlotStyle(PlotStyle::Bars);
    EXPECT_EQ(widget.plotStyle(), PlotStyle::Bars);
}

TEST_F(QualityDistributionWidgetTest, ThresholdSettings)
{
    QualityDistributionWidget widget;
    widget.setMinScoreThreshold(40.0);
    widget.setMinStarsThreshold(100);
    // No getter for threshold, just ensure it doesn't crash
    widget.setMinSnrThreshold(12.0);
    widget.setMaxFwhmThreshold(5.0);
    widget.setMaxEccentricityThreshold(0.8);
    widget.clearThresholds();
}

TEST_F(QualityDistributionWidgetTest, Selection)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "/path/to/1";
    points.append(p1);
    QVector<ChartGroupInfo> groups;
    widget.setData(points, groups);

    widget.setSelectedFrameId("/path/to/1");
    EXPECT_EQ(widget.selectedFrameId(), "/path/to/1");

    widget.clearSelection();
    EXPECT_EQ(widget.selectedFrameId(), "");
}

// More advanced tests could verify painting results or internal state if exposed,
// but for a black-box UI test, verifying property setters/getters and no-crash on paint is good.

TEST_F(QualityDistributionWidgetTest, PaintEventNoCrash)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "/path/to/1";
    p1.score = 10.0;
    points.append(p1);
    QVector<ChartGroupInfo> groups;
    widget.setData(points, groups);

    widget.resize(400, 300);
    widget.show();
    // In a real GUI test we might grab the pixmap, but here just ensure render code runs
    widget.repaint();
}

TEST_F(QualityDistributionWidgetTest, NormalizationMode)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;

    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    p1.filterName = "Ha";
    points.append(p1);

    ChartDataPoint p2;
    p2.frameId = "2";
    p2.score = 20.0;
    p2.filterName = "Ha";
    points.append(p2);

    QVector<ChartGroupInfo> groups;
    widget.setData(points, groups);

    // Normalization should be off by default
    EXPECT_EQ(widget.normalizeMode(), NormalizeMode::Off);

    // Change to by filter
    widget.setNormalizeMode(NormalizeMode::ByFilter);
    EXPECT_EQ(widget.normalizeMode(), NormalizeMode::ByFilter);

    // Changing normalization mode should trigger re-paint and data refresh
    widget.repaint();
}

TEST_F(QualityDistributionWidgetTest, ExportToImageCreatesFile)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    points.append(p1);
    QVector<ChartGroupInfo> groups;
    widget.setData(points, groups);

    widget.resize(400, 300);
    // Ensure it's painted
    widget.repaint();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString tempFile = tempDir.filePath(QStringLiteral("test_chart_export.png"));

    EXPECT_TRUE(widget.exportChartImage(tempFile, "PNG"));
    EXPECT_TRUE(QFile::exists(tempFile));
}

TEST_F(QualityDistributionWidgetTest, ExportToCsvCreatesFile)
{
    QualityDistributionWidget widget;
    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "frame1";
    p1.displayName = "frame1.xisf";
    p1.score = 10.5;
    p1.filterName = "Red";
    points.append(p1);
    QVector<ChartGroupInfo> groups;
    widget.setData(points, groups);

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString tempFile = tempDir.filePath(QStringLiteral("test_chart_data.csv"));

    EXPECT_TRUE(widget.exportChartDataToCsv(tempFile));
    EXPECT_TRUE(QFile::exists(tempFile));

    QFile f(tempFile);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    f.close();

    EXPECT_TRUE(content.contains("Frame,Group,Filter"));
    EXPECT_TRUE(content.contains("frame1.xisf"));
    EXPECT_TRUE(content.contains("Red"));
    EXPECT_TRUE(content.contains("10.5"));
}

static QCheckBox* FindCheckBoxByText(QWidget& parent, const QString& text)
{
    const auto boxes = parent.findChildren<QCheckBox*>();
    for (auto* box : boxes)
    {
        if (box && box->text() == text)
        {
            return box;
        }
    }
    return nullptr;
}

TEST_F(QualityDistributionWidgetTest, DefaultCheckboxStates)
{
    QualityDistributionWidget widget;

    auto* percentile = FindCheckBoxByText(widget, QObject::tr("Percentile band"));
    auto* median = FindCheckBoxByText(widget, QObject::tr("Median lines"));
    auto* groupByFilter = FindCheckBoxByText(widget, QObject::tr("Group by filter"));
    auto* dropToBaseline = FindCheckBoxByText(widget, QObject::tr("Drop to baseline"));

    ASSERT_NE(percentile, nullptr);
    ASSERT_NE(median, nullptr);
    ASSERT_NE(groupByFilter, nullptr);
    ASSERT_NE(dropToBaseline, nullptr);

    EXPECT_TRUE(percentile->isChecked());
    EXPECT_TRUE(median->isChecked());
    EXPECT_FALSE(groupByFilter->isChecked());
    EXPECT_FALSE(dropToBaseline->isChecked());
    EXPECT_FALSE(dropToBaseline->isEnabled());
}

TEST_F(QualityDistributionWidgetTest, PercentileBandToggleUpdatesState)
{
    QualityDistributionWidget widget;
    auto* percentile = FindCheckBoxByText(widget, QObject::tr("Percentile band"));
    ASSERT_NE(percentile, nullptr);

    percentile->setChecked(false);
    EXPECT_FALSE(widget.showPercentileBand());

    percentile->setChecked(true);
    EXPECT_TRUE(widget.showPercentileBand());
}

TEST_F(QualityDistributionWidgetTest, GroupByFilterEnablesDropToBaseline)
{
    QualityDistributionWidget widget;

    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    p1.filterName = "R";
    points.append(p1);

    ChartDataPoint p2;
    p2.frameId = "2";
    p2.score = 20.0;
    p2.filterName = "G";
    points.append(p2);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);

    auto* groupByFilter = FindCheckBoxByText(widget, QObject::tr("Group by filter"));
    auto* dropToBaseline = FindCheckBoxByText(widget, QObject::tr("Drop to baseline"));
    ASSERT_NE(groupByFilter, nullptr);
    ASSERT_NE(dropToBaseline, nullptr);

    EXPECT_TRUE(groupByFilter->isEnabled());
    EXPECT_FALSE(groupByFilter->isChecked());
    EXPECT_FALSE(dropToBaseline->isEnabled());

    groupByFilter->setChecked(true);
    EXPECT_TRUE(dropToBaseline->isEnabled());
}

TEST_F(QualityDistributionWidgetTest, GroupByFilterDisabledWithSingleFilter)
{
    QualityDistributionWidget widget;

    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    p1.filterName = "R";
    points.append(p1);

    ChartDataPoint p2;
    p2.frameId = "2";
    p2.score = 20.0;
    p2.filterName = "R";
    points.append(p2);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);

    auto* groupByFilter = FindCheckBoxByText(widget, QObject::tr("Group by filter"));
    auto* dropToBaseline = FindCheckBoxByText(widget, QObject::tr("Drop to baseline"));
    ASSERT_NE(groupByFilter, nullptr);
    ASSERT_NE(dropToBaseline, nullptr);

    EXPECT_FALSE(groupByFilter->isEnabled());
    EXPECT_FALSE(groupByFilter->isChecked());
    EXPECT_FALSE(dropToBaseline->isEnabled());
}

TEST_F(QualityDistributionWidgetTest, KeyboardNavigationEmitsFrameClicked)
{
    QualityDistributionWidget widget;

    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    points.append(p1);

    ChartDataPoint p2;
    p2.frameId = "2";
    p2.score = 20.0;
    points.append(p2);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);

    QString clickedId;
    QObject::connect(&widget, &QualityDistributionWidget::frameClicked,
                     [&](const QString& frameId) { clickedId = frameId; });

    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&widget, &keyEvent);

    EXPECT_EQ(widget.selectedFrameId(), "1");
    EXPECT_EQ(clickedId, "1");

    QKeyEvent keyEvent2(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&widget, &keyEvent2);

    EXPECT_EQ(widget.selectedFrameId(), "2");
    EXPECT_EQ(clickedId, "2");
}

TEST_F(QualityDistributionWidgetTest, DoubleClickEmitsFrameDoubleClicked)
{
    QualityDistributionWidget widget;

    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    points.append(p1);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);
    widget.resize(600, 300);
    widget.show();
    QApplication::processEvents();

    QString doubleClickedId;
    QObject::connect(&widget, &QualityDistributionWidget::frameDoubleClicked,
                     [&](const QString& frameId) { doubleClickedId = frameId; });

    // Click near the left side of the plot area (index 0)
    const QPoint clickPos(70, widget.height() / 2);
    QMouseEvent dblClickEvent(QEvent::MouseButtonDblClick, QPointF(clickPos), QPointF(widget.mapToGlobal(clickPos)),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &dblClickEvent);

    EXPECT_EQ(doubleClickedId, "1");
}

TEST_F(QualityDistributionWidgetTest, ContextMenuEmitsFrameContextMenuRequested)
{
    QualityDistributionWidget widget;

    QVector<ChartDataPoint> points;
    ChartDataPoint p1;
    p1.frameId = "1";
    p1.score = 10.0;
    points.append(p1);

    QVector<ChartGroupInfo> groups;
    ChartGroupInfo g1;
    g1.key = "group1";
    groups.append(g1);

    widget.setData(points, groups);
    widget.resize(600, 300);
    widget.show();
    QApplication::processEvents();

    QString contextFrameId;
    QObject::connect(&widget, &QualityDistributionWidget::frameContextMenuRequested,
                     [&](const QString& frameId, const QPoint&) { contextFrameId = frameId; });

    const QPoint clickPos(70, widget.height() / 2);
    QContextMenuEvent ctxEvent(QContextMenuEvent::Mouse, clickPos, widget.mapToGlobal(clickPos));
    QApplication::sendEvent(&widget, &ctxEvent);

    EXPECT_EQ(contextFrameId, "1");
}
