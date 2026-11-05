// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QScrollArea>

#include "widgets/CurvesWidget.h"
#include "widgets/ToneCurveModel.h"

TEST(WidgetsCurves, GuideAndHistogramFlagsCanBeToggled)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);

    widget.setGuideLineEnabled(false);
    widget.setHistogramEnabled(false);

    EXPECT_FALSE(widget.getGuideLineEnabled());
    EXPECT_FALSE(widget.getHistogramEnabled());
}

TEST(WidgetsCurves, CurveIdentityIsDetected)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);

    widget.setCurve(an::ui::ToneCurveModel({{0.0, 0.0}, {1.0, 1.0}}, an::CurveInterpolation::Linear));
    EXPECT_TRUE(widget.curveIsIdentity());
}

TEST(WidgetsCurves, CurveInterpolationRoundTrip)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);

    widget.setCurve(an::ui::ToneCurveModel({{0.0, 0.0}, {1.0, 1.0}}, an::CurveInterpolation::Linear));

    widget.setCurveInterpolation(an::CurveInterpolation::NaturalCubic);
    EXPECT_EQ(widget.getCurveInterpolation(), an::CurveInterpolation::NaturalCubic);
}
