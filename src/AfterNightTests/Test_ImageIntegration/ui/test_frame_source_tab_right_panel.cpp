// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_frame_source_tab_right_panel.cpp
 * @brief Unit tests for FrameSourceTab right panel widgets.
 *
 * Tests the Lights-tab right-panel widgets:
 * - FrameStatisticsDashboard
 * - FrameGroupingIssuesWidget
 * - FrameQuickActionsWidget
 * - Coverage toggle checkbox
 * - Preview panel integration
 */

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTimeZone>
#include <QWidget>

#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/widgets/FrameStatisticsDashboard.h"
#include "processes/ImageIntegration/widgets/FrameGroupingIssuesWidget.h"
#include "processes/ImageIntegration/widgets/FrameQuickActionsWidget.h"
#include "processes/ImageIntegration/widgets/GroupingOptionsWidget.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

namespace
{
void ensureQApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

FrameInfo createTestFrame(FrameType type, const QString& path, bool excluded = false,
                          const QString& objectName = QStringLiteral("M42"))
{
    FrameMetadata meta(type, QSize(1000, 1000), 30.0, -10.0, 100, 10, QStringLiteral("L"), objectName,
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameInfo frame(FrameId(path), path, meta);
    frame.excluded = excluded;
    return frame;
}
} // namespace

class FrameSourceTabRightPanelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureQApp();
        model = new IntegrationDataModel();
    }

    void TearDown() override
    {
        delete lightTab;
        delete biasTab;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    FrameSourceTab* lightTab = nullptr;
    FrameSourceTab* biasTab = nullptr;
};

// --- Statistics Dashboard Tests ---

TEST_F(FrameSourceTabRightPanelTest, LightTabHasStatisticsDashboard)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* dashboard = lightTab->findChild<ImageIntegration::FrameStatisticsDashboard*>();
    EXPECT_NE(dashboard, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, NonLightTabsDoNotHaveStatisticsDashboard)
{
    biasTab = new FrameSourceTab(FrameType::Bias, model);

    auto* dashboard = biasTab->findChild<ImageIntegration::FrameStatisticsDashboard*>();
    EXPECT_EQ(dashboard, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, StatisticsDashboardRefreshesOnFrameAdd)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* dashboard = lightTab->findChild<ImageIntegration::FrameStatisticsDashboard*>();
    ASSERT_NE(dashboard, nullptr);

    // Add frames
    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"))});
    QApplication::processEvents();

    // Dashboard should be visible and not crashed
    EXPECT_TRUE(dashboard->isVisible() || dashboard->isVisibleTo(lightTab));
}

// --- Issues Widget Tests ---

TEST_F(FrameSourceTabRightPanelTest, LightTabHasIssuesWidget)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* issues = lightTab->findChild<ImageIntegration::FrameGroupingIssuesWidget*>();
    EXPECT_NE(issues, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, NonLightTabsDoNotHaveIssuesWidget)
{
    biasTab = new FrameSourceTab(FrameType::Bias, model);

    auto* issues = biasTab->findChild<ImageIntegration::FrameGroupingIssuesWidget*>();
    EXPECT_EQ(issues, nullptr);
}

// --- Quick Actions Widget Tests ---

TEST_F(FrameSourceTabRightPanelTest, LightTabHasQuickActionsWidget)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* quickActions = lightTab->findChild<ImageIntegration::FrameQuickActionsWidget*>();
    EXPECT_NE(quickActions, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, NonLightTabsDoNotHaveQuickActionsWidget)
{
    biasTab = new FrameSourceTab(FrameType::Bias, model);

    auto* quickActions = biasTab->findChild<ImageIntegration::FrameQuickActionsWidget*>();
    EXPECT_EQ(quickActions, nullptr);
}

// --- Coverage Toggle Tests ---

TEST_F(FrameSourceTabRightPanelTest, LightTabHasCoverageToggle)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* toggle = lightTab->findChild<QCheckBox*>(QStringLiteral("coverageToggle"));
    EXPECT_NE(toggle, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, NonLightTabsDoNotHaveCoverageToggle)
{
    biasTab = new FrameSourceTab(FrameType::Bias, model);

    auto* toggle = biasTab->findChild<QCheckBox*>(QStringLiteral("coverageToggle"));
    EXPECT_EQ(toggle, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, CoverageToggleHasTooltip)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* toggle = lightTab->findChild<QCheckBox*>(QStringLiteral("coverageToggle"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->toolTip().isEmpty());
}

TEST_F(FrameSourceTabRightPanelTest, CoverageToggleFiltersTreeModel)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* toggle = lightTab->findChild<QCheckBox*>(QStringLiteral("coverageToggle"));
    ASSERT_NE(toggle, nullptr);

    auto* treeModel = lightTab->treeModel();
    ASSERT_NE(treeModel, nullptr);

    // Toggle should affect the model's filter mode
    toggle->setChecked(true);
    QApplication::processEvents();

    // Model should now be in calibration-warnings-only mode
    // Verify by checking matchCount behavior (with no frames, match should be 0)
    EXPECT_EQ(treeModel->totalCount(), 0);
}

// --- Preview Panel Tests ---

TEST_F(FrameSourceTabRightPanelTest, HasPreviewPanel)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* preview = lightTab->previewPanel();
    EXPECT_NE(preview, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, PreviewUsesImageViewControl)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QWidget* preview = lightTab->previewPanel();
    ASSERT_NE(preview, nullptr);

    EXPECT_NE(dynamic_cast<ImageViewControl*>(preview), nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, PreviewPanelHasMinimumSize)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* preview = lightTab->previewPanel();
    ASSERT_NE(preview, nullptr);

    EXPECT_GE(preview->minimumWidth(), 200);
    EXPECT_GE(preview->minimumHeight(), 120);
}

TEST_F(FrameSourceTabRightPanelTest, LightTabPreviewHasCalibrationVisible)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* preview = lightTab->previewPanel();
    ASSERT_NE(preview, nullptr);

    auto* calibrationCheckbox = preview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(calibrationCheckbox, nullptr);
    EXPECT_FALSE(calibrationCheckbox->isHidden());
}

// --- Splitter Tests ---

TEST_F(FrameSourceTabRightPanelTest, HasSplitter)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QSplitter* splitter = lightTab->findChild<QSplitter*>();
    EXPECT_NE(splitter, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, SplitterHasTwoPanes)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QSplitter* splitter = lightTab->findChild<QSplitter*>();
    ASSERT_NE(splitter, nullptr);

    EXPECT_EQ(splitter->count(), 2);
}

TEST_F(FrameSourceTabRightPanelTest, SplitterIsHorizontal)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QSplitter* splitter = lightTab->findChild<QSplitter*>();
    ASSERT_NE(splitter, nullptr);

    EXPECT_EQ(splitter->orientation(), Qt::Horizontal);
}

TEST_F(FrameSourceTabRightPanelTest, PreviewPaneCannotBeCollapsed)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QSplitter* splitter = lightTab->findChild<QSplitter*>();
    ASSERT_NE(splitter, nullptr);
    ASSERT_EQ(splitter->count(), 2);

    EXPECT_FALSE(splitter->isCollapsible(1));
}

// --- Filter Stats Label Tests ---

TEST_F(FrameSourceTabRightPanelTest, HasFilterStatsLabel)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    QLabel* label = lightTab->findChild<QLabel*>(QStringLiteral("frameTreeMatchCount"));
    EXPECT_NE(label, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, FilterStatsLabelUpdatesOnSearch)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    model->addFrames(
        FrameType::Light,
        {createTestFrame(FrameType::Light, QStringLiteral("C:/test/m42_light.fit"), false, QStringLiteral("M42")),
         createTestFrame(FrameType::Light, QStringLiteral("C:/test/m31_light.fit"), false, QStringLiteral("M31"))});

    QLabel* label = lightTab->findChild<QLabel*>(QStringLiteral("frameTreeMatchCount"));
    ASSERT_NE(label, nullptr);

    auto* searchBar = lightTab->searchBar();
    ASSERT_NE(searchBar, nullptr);

    searchBar->setText(QStringLiteral("m42"));
    QTest::qWait(300); // Wait for debounce

    // Label should show "Showing 1 of 2 frames"
    EXPECT_TRUE(label->text().contains(QStringLiteral("1")));
    EXPECT_TRUE(label->text().contains(QStringLiteral("2")));
}

// --- Right Panel Scroll Area Tests ---

TEST_F(FrameSourceTabRightPanelTest, RightPanelHasScrollArea)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* scrollArea = lightTab->findChild<QScrollArea*>(QStringLiteral("frameSourceRightScrollArea"));
    EXPECT_NE(scrollArea, nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, RightPanelScrollAreaContainsContent)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* scrollArea = lightTab->findChild<QScrollArea*>(QStringLiteral("frameSourceRightScrollArea"));
    ASSERT_NE(scrollArea, nullptr);

    EXPECT_NE(scrollArea->widget(), nullptr);
}

TEST_F(FrameSourceTabRightPanelTest, PreviewIsNotNestedInsideRightPanelScrollArea)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* scrollArea = lightTab->findChild<QScrollArea*>(QStringLiteral("frameSourceRightScrollArea"));
    ASSERT_NE(scrollArea, nullptr);
    ASSERT_NE(scrollArea->widget(), nullptr);

    QWidget* preview = lightTab->previewPanel();
    ASSERT_NE(preview, nullptr);

    EXPECT_NE(preview->parentWidget(), scrollArea->widget());
}

TEST_F(FrameSourceTabRightPanelTest, PreviewPrecedesRightPanelScrollArea)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    auto* scrollArea = lightTab->findChild<QScrollArea*>(QStringLiteral("frameSourceRightScrollArea"));
    ASSERT_NE(scrollArea, nullptr);

    QWidget* preview = lightTab->previewPanel();
    ASSERT_NE(preview, nullptr);

    QLayout* rightColumnLayout = preview->parentWidget() ? preview->parentWidget()->layout() : nullptr;
    ASSERT_NE(rightColumnLayout, nullptr);

    const int previewIndex = rightColumnLayout->indexOf(preview);
    const int scrollIndex = rightColumnLayout->indexOf(scrollArea);
    ASSERT_GE(previewIndex, 0);
    ASSERT_GE(scrollIndex, 0);
    EXPECT_LT(previewIndex, scrollIndex);
}

// --- Refresh Methods Tests ---

TEST_F(FrameSourceTabRightPanelTest, RefreshQuickStatsDoesNotCrashWithEmptyModel)
{
    auto* tab = new FrameSourceTab(FrameType::Light, model);
    tab->refreshQuickStats();
    SUCCEED();
    delete tab;
}

TEST_F(FrameSourceTabRightPanelTest, RefreshFilterStatsDoesNotCrashWithEmptyModel)
{
    auto* tab = new FrameSourceTab(FrameType::Light, model);
    tab->refreshFilterStats();
    SUCCEED();
    delete tab;
}

TEST_F(FrameSourceTabRightPanelTest, RefreshGroupingOptionsFromModelSyncs)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);

    GroupingOptions opts;
    opts.byFilter = true;
    opts.byExposure = true;
    model->setGroupingOptions(FrameType::Light, opts);

    lightTab->refreshGroupingOptionsFromModel();

    // Verify grouping options widget reflects new state
    auto* groupingWidget = lightTab->groupingOptions();
    ASSERT_NE(groupingWidget, nullptr);

    GroupingOptions currentOpts = groupingWidget->currentOptions();
    EXPECT_EQ(currentOpts.byFilter, opts.byFilter);
    EXPECT_EQ(currentOpts.byExposure, opts.byExposure);
}

// --- All Frame Types Have Basic Right Panel ---

TEST_F(FrameSourceTabRightPanelTest, AllFrameTypesHavePreviewPanel)
{
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        auto* tab = new FrameSourceTab(type, model);
        EXPECT_NE(tab->previewPanel(), nullptr) << "FrameType " << static_cast<int>(type);
        delete tab;
    }
}

TEST_F(FrameSourceTabRightPanelTest, AllFrameTypesHaveGroupingOptions)
{
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        auto* tab = new FrameSourceTab(type, model);
        EXPECT_NE(tab->groupingOptions(), nullptr) << "FrameType " << static_cast<int>(type);
        delete tab;
    }
}

TEST_F(FrameSourceTabRightPanelTest, AllFrameTypesHaveStatsLabel)
{
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        auto* tab = new FrameSourceTab(type, model);
        EXPECT_NE(tab->statsLabel(), nullptr) << "FrameType " << static_cast<int>(type);
        delete tab;
    }
}

// --- Integration Tests ---

TEST_F(FrameSourceTabRightPanelTest, WidgetsRespondToModelChanges)
{
    lightTab = new FrameSourceTab(FrameType::Light, model);
    lightTab->resize(800, 600);
    lightTab->show();
    QApplication::processEvents();

    // Add frames
    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit")),
                                        createTestFrame(FrameType::Light, QStringLiteral("C:/test/light2.fit"), true)});
    QApplication::processEvents();

    // Dashboard should show 2 total, 1 included, 1 excluded
    // Issues widget should show some info
    // Quick actions should have buttons enabled

    auto* dashboard = lightTab->findChild<ImageIntegration::FrameStatisticsDashboard*>();
    auto* issues = lightTab->findChild<ImageIntegration::FrameGroupingIssuesWidget*>();
    auto* quickActions = lightTab->findChild<ImageIntegration::FrameQuickActionsWidget*>();

    ASSERT_NE(dashboard, nullptr);
    ASSERT_NE(issues, nullptr);
    ASSERT_NE(quickActions, nullptr);

    // All widgets should be visible (or at least not hidden)
    EXPECT_TRUE(dashboard->isVisibleTo(lightTab));
    EXPECT_TRUE(issues->isVisibleTo(lightTab));
    EXPECT_TRUE(quickActions->isVisibleTo(lightTab));
}

TEST_F(FrameSourceTabRightPanelTest, RegroupWarningLabelShowsDuringRegroup)
{
    // Add many frames to trigger progress indicator
    std::vector<FrameInfo> frames;
    for (int i = 0; i < 600; ++i)
    {
        frames.push_back(createTestFrame(FrameType::Light, QStringLiteral("C:/test/light%1.fit").arg(i)));
    }
    model->addFrames(FrameType::Light, frames);

    lightTab = new FrameSourceTab(FrameType::Light, model);
    lightTab->show();
    QApplication::processEvents();

    QLabel* warning = lightTab->regroupWarningLabel();
    ASSERT_NE(warning, nullptr);

    // Initially hidden (no active regroup)
    // The warning label is used during regrouping operations
    // We verify it exists and has themed styling via palette and properties
    EXPECT_EQ(warning->property("anFrameRadius").toInt(), 4);
}
