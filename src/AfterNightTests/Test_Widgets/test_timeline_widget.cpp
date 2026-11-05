// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "widgets/TimelineStageWidget.h"

TEST(WidgetsTimeline, DefaultStateIsPending)
{
    TimelineStageWidget widget;
    EXPECT_EQ(widget.state(), TimelineStageWidget::StageState::Pending);
}

TEST(WidgetsTimeline, StageNameAndStateRoundTrip)
{
    TimelineStageWidget widget;

    widget.setStageName(QStringLiteral("Calibration"));
    widget.setState(TimelineStageWidget::StageState::Active);

    EXPECT_EQ(widget.stageName(), QStringLiteral("Calibration"));
    EXPECT_EQ(widget.state(), TimelineStageWidget::StageState::Active);
}

TEST(WidgetsTimeline, TooltipIncludesStageMetadata)
{
    TimelineStageWidget widget;

    widget.setStageName(QStringLiteral("Integration"));
    widget.setGroupInfo(QStringLiteral("3/10"));
    widget.setElapsedTime(QStringLiteral("00:12:34"));
    widget.setState(TimelineStageWidget::StageState::Completed);

    const QString tooltip = widget.toolTip();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Integration")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Completed")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("00:12:34")));
}
