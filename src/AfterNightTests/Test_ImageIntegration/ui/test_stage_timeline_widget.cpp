// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QEvent>
#include <QPainter>

#include "processes/ImageIntegration/widgets/StageTimelineWidget.h"

// The suite's harness provides the headless application.
class StageTimelineWidgetTest : public ::testing::Test
{
};

TEST_F(StageTimelineWidgetTest, InitialState)
{
    StageTimelineWidget widget;
    EXPECT_EQ(widget.currentStage(), -1);
}

TEST_F(StageTimelineWidgetTest, SetStagesInitializesState)
{
    StageTimelineWidget widget;
    QStringList stages = {"Stage 1", "Stage 2", "Stage 3"};
    widget.setStages(stages);

    EXPECT_EQ(widget.currentStage(), 0);
    EXPECT_EQ(widget.stageStatus(0), StageTimelineStatus::Pending);
    EXPECT_EQ(widget.stageStatus(2), StageTimelineStatus::Pending);
}

TEST_F(StageTimelineWidgetTest, SetCurrentStageUpdatesIndex)
{
    StageTimelineWidget widget;
    widget.setStages({"A", "B", "C"});

    widget.setCurrentStage(1);
    EXPECT_EQ(widget.currentStage(), 1);

    // Out of bounds check
    widget.setCurrentStage(10);
    EXPECT_EQ(widget.currentStage(), 1); // Should not change
}

TEST_F(StageTimelineWidgetTest, SetStageStatusUpdatesStatus)
{
    StageTimelineWidget widget;
    widget.setStages({"A", "B"});

    widget.setStageStatus(0, StageTimelineStatus::Completed);
    EXPECT_EQ(widget.stageStatus(0), StageTimelineStatus::Completed);

    widget.setStageStatus(1, StageTimelineStatus::InProgress);
    EXPECT_EQ(widget.stageStatus(1), StageTimelineStatus::InProgress);
}

TEST_F(StageTimelineWidgetTest, ThemeChangeEventsDoNotCrash)
{
    StageTimelineWidget widget;
    widget.setStages({"Calibration", "Registration", "Stacking"});
    widget.resize(480, 140);

    QEvent paletteChange(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(&widget, &paletteChange);

    QEvent themeChange(QEvent::ThemeChange);
    QApplication::sendEvent(&widget, &themeChange);

    SUCCEED();
}
