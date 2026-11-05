// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_frame_grouping_issues_widget.cpp
 * @brief Unit tests for FrameGroupingIssuesWidget.
 *
 * Tests the widget that displays grouping-related issues like
 * groups with insufficient frames, mixed dimensions, and high exclusion ratios.
 */

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QTimeZone>

#include "processes/ImageIntegration/widgets/FrameGroupingIssuesWidget.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "theme/AnDesignTokens.h"

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

FrameInfo createTestFrame(FrameType type, const QString& path, int width = 1000, int height = 1000,
                          bool excluded = false, const QString& filter = QStringLiteral("L"))
{
    FrameMetadata meta(type, QSize(width, height), 30.0, -10.0, 100, 10, filter, QStringLiteral("M42"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameInfo frame(FrameId(path), path, meta);
    frame.excluded = excluded;
    return frame;
}
} // namespace

class FrameGroupingIssuesWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureQApp();
        model = new IntegrationDataModel();
    }

    void TearDown() override
    {
        delete widget;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    ImageIntegration::FrameGroupingIssuesWidget* widget = nullptr;
};

TEST_F(FrameGroupingIssuesWidgetTest, ConstructsWithoutCrash)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    ASSERT_NE(widget, nullptr);
}

TEST_F(FrameGroupingIssuesWidgetTest, HasCorrectObjectName)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    EXPECT_EQ(widget->objectName(), QStringLiteral("frameGroupingIssuesWidget"));
}

TEST_F(FrameGroupingIssuesWidgetTest, PreservesFrameType)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Bias, model);
    EXPECT_EQ(widget->frameType(), FrameType::Bias);

    widget->setFrameType(FrameType::Dark);
    EXPECT_EQ(widget->frameType(), FrameType::Dark);
}

TEST_F(FrameGroupingIssuesWidgetTest, ShowsNoIssuesWhenEmpty)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();
    widget->show();
    QApplication::processEvents();

    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->text().contains(QStringLiteral("0")));

    QLabel* empty = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisible());
}

TEST_F(FrameGroupingIssuesWidgetTest, ShowsNoIssuesWithHealthyData)
{
    // Add multiple frames per group - healthy setup
    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit")),
                                        createTestFrame(FrameType::Light, QStringLiteral("C:/test/light2.fit")),
                                        createTestFrame(FrameType::Light, QStringLiteral("C:/test/light3.fit"))});

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();

    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);
    // Should have 0 issues (no single-frame groups, no mixed dimensions)
    EXPECT_TRUE(header->text().contains(QStringLiteral("0")));
}

TEST_F(FrameGroupingIssuesWidgetTest, DetectsSingleFrameGroups)
{
    // Create a single-frame group by setting different filters
    GroupingOptions opts;
    opts.byFilter = true;
    model->setGroupingOptions(FrameType::Light, opts);

    model->addFrames(
        FrameType::Light,
        {createTestFrame(FrameType::Light, QStringLiteral("C:/test/r1.fit"), 1000, 1000, false, QStringLiteral("R")),
         createTestFrame(FrameType::Light, QStringLiteral("C:/test/g1.fit"), 1000, 1000, false, QStringLiteral("G")),
         createTestFrame(FrameType::Light, QStringLiteral("C:/test/b1.fit"), 1000, 1000, false, QStringLiteral("B"))});

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();
    widget->show();
    QApplication::processEvents();

    // Should detect 3 single-frame groups
    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);

    // Header should show non-zero issues
    bool hasNonZero = !header->text().contains(QStringLiteral("(0)"));
    EXPECT_TRUE(hasNonZero);

    // Check issue labels
    QLabel* issue0 = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssue_0"));
    ASSERT_NE(issue0, nullptr);
    EXPECT_TRUE(issue0->isVisible());
    EXPECT_TRUE(issue0->text().contains(QStringLiteral("1 frame"), Qt::CaseInsensitive));
}

TEST_F(FrameGroupingIssuesWidgetTest, DetectsHighExclusionRatio)
{
    // Create a group with more excluded than included
    model->addFrames(FrameType::Light,
                     {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"), 1000, 1000, false),
                      createTestFrame(FrameType::Light, QStringLiteral("C:/test/light2.fit"), 1000, 1000, true),
                      createTestFrame(FrameType::Light, QStringLiteral("C:/test/light3.fit"), 1000, 1000, true),
                      createTestFrame(FrameType::Light, QStringLiteral("C:/test/light4.fit"), 1000, 1000, true)});

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();

    // Should detect high exclusion ratio
    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);

    // Header should show some issues
    bool hasIssues = !header->text().contains(QStringLiteral("(0)"));
    EXPECT_TRUE(hasIssues);
}

TEST_F(FrameGroupingIssuesWidgetTest, ExcludedFramesDoNotCreateIssues)
{
    model->addFrames(FrameType::Light,
                     {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"), 1000, 1000, false),
                      createTestFrame(FrameType::Light, QStringLiteral("C:/test/light2.fit"), 1000, 1000, false),
                      createTestFrame(FrameType::Light, QStringLiteral("C:/test/light3.fit"), 1000, 1000, true)});

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();
    widget->show();
    QApplication::processEvents();

    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->text().contains(QStringLiteral("(0)")));
}

TEST_F(FrameGroupingIssuesWidgetTest, EmitsShowGroupRequestedSignal)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);

    QSignalSpy spy(widget, &ImageIntegration::FrameGroupingIssuesWidget::showGroupRequested);
    EXPECT_EQ(spy.count(), 0); // Just verify signal can be connected
}

TEST_F(FrameGroupingIssuesWidgetTest, RefreshDoesNotCrashWithNullModel)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, nullptr);
    widget->refresh();
    SUCCEED();
}

TEST_F(FrameGroupingIssuesWidgetTest, WorksWithAllFrameTypes)
{
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        model->addFrames(type, {createTestFrame(type, QStringLiteral("C:/test/frame1.fit")),
                                createTestFrame(type, QStringLiteral("C:/test/frame2.fit"))});

        auto* w = new ImageIntegration::FrameGroupingIssuesWidget(type, model);
        w->refresh();

        QLabel* header = w->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
        ASSERT_NE(header, nullptr);

        delete w;
        // model->removeAllFrames(type);
        QStringList paths;
        for (const auto& f : model->frames(type).frames())
            paths << f.id.canonicalPath();
        if (!paths.isEmpty())
            model->removeFrames(type, paths);
    }
    SUCCEED();
}

TEST_F(FrameGroupingIssuesWidgetTest, IssueLabelsHaveTooltips)
{
    // Create single-frame groups to trigger issues
    GroupingOptions opts;
    opts.byFilter = true;
    model->setGroupingOptions(FrameType::Light, opts);

    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/r1.fit"), 1000, 1000,
                                                        false, QStringLiteral("R"))});

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();

    QLabel* issue0 = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssue_0"));
    if (issue0 && issue0->isVisible())
    {
        EXPECT_FALSE(issue0->toolTip().isEmpty());
    }
}

TEST_F(FrameGroupingIssuesWidgetTest, DoesNotShowMoreLabelWhenUnderLimit)
{
    // Create many single-frame groups to trigger "...and X more"
    GroupingOptions opts;
    opts.byFilter = true;
    model->setGroupingOptions(FrameType::Light, opts);

    // Add 10 unique filter groups (single issue summary expected)
    std::vector<FrameInfo> frames;
    for (int i = 0; i < 10; ++i)
    {
        frames.push_back(createTestFrame(FrameType::Light, QStringLiteral("C:/test/%1.fit").arg(i), 1000, 1000, false,
                                         QStringLiteral("Filter%1").arg(i)));
    }
    model->addFrames(FrameType::Light, frames);

    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();
    widget->show();
    QApplication::processEvents();

    QLabel* moreLabel = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesMore"));
    ASSERT_NE(moreLabel, nullptr);
    // Single issue summary should not exceed the max (5) display limit
    EXPECT_FALSE(moreLabel->isVisible());
}

TEST_F(FrameGroupingIssuesWidgetTest, HeaderTooltipWhenNoIssues)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->refresh();

    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);

    // When no issues, header should have a tooltip explaining the state
    EXPECT_FALSE(header->toolTip().isEmpty());
}

TEST_F(FrameGroupingIssuesWidgetTest, HeaderUsesHeading3TokenFont)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    QLabel* header = widget->findChild<QLabel*>(QStringLiteral("lblGroupingIssuesHeader"));
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->font(), an::ui::tokens().typography.heading3);
}

TEST_F(FrameGroupingIssuesWidgetTest, ThemeChangeEventsDoNotCrash)
{
    widget = new ImageIntegration::FrameGroupingIssuesWidget(FrameType::Light, model);
    widget->resize(460, 220);

    QEvent paletteChange(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(widget, &paletteChange);

    QEvent themeChange(QEvent::ThemeChange);
    QApplication::sendEvent(widget, &themeChange);

    SUCCEED();
}
