// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The tone curve a curves widget edits: its editing rules and fallbacks, the widget over it, and the curve the widget
/// hands on held to the curves the behaviour expectations record, sample by sample.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "fixtures/common/FixtureExpectation.h"
#include "widgets/CurvesWidget.h"
#include "widgets/ToneCurveModel.h"

#include <QImage>
#include <QMouseEvent>
#include <QScrollArea>

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;

/// Keeps every curve the widget reports.
class RecordingListener : public CurvesWidgetListener
{
public:
    void onCurvesUpdated(const an::ui::ToneCurveModel& curve) override
    {
        ++updates;
        last = curve;
    }

    int updates = 0;
    an::ui::ToneCurveModel last;
};

[[nodiscard]] an::ui::ToneCurveModel threePoints(const an::CurveInterpolation interpolation)
{
    return an::ui::ToneCurveModel({{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}}, interpolation);
}

[[nodiscard]] an::ui::ToneCurveModel fivePoints(const an::CurveInterpolation interpolation)
{
    return an::ui::ToneCurveModel({{0.0, 0.0}, {0.2, 0.3}, {0.4, 0.55}, {0.7, 0.8}, {1.0, 1.0}}, interpolation);
}

/// A widget the cases click on: wide enough that a point is many pixels from its neighbours.
struct ClickableCurves
{
    QScrollArea parent;
    CurvesWidget widget{&parent};

    ClickableCurves()
    {
        widget.setGradientAreaEnabled(false);
        widget.resize(400, 200);
    }

    /// The widget position of a point of the unit square.
    [[nodiscard]] QPointF at(const double input, const double output) const
    {
        return QPointF(input * widget.width(), (1.0 - output) * widget.height());
    }

    void press(const QPointF& position, const Qt::MouseButton button)
    {
        QMouseEvent event(QEvent::MouseButtonPress, position, widget.mapToGlobal(position), button, button,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&widget, &event);
    }

    void move(const QPointF& position)
    {
        QMouseEvent event(QEvent::MouseMove, position, widget.mapToGlobal(position), Qt::NoButton, Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&widget, &event);
    }

    void release(const QPointF& position, const Qt::MouseButton button)
    {
        QMouseEvent event(QEvent::MouseButtonRelease, position, widget.mapToGlobal(position), button, Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&widget, &event);
    }
};

} // namespace

TEST(ToneCurveModelTest, TheDefaultIsTheIdentity)
{
    const an::ui::ToneCurveModel curve;
    EXPECT_TRUE(curve.isIdentity());
    EXPECT_EQ(curve.size(), 2u);
    EXPECT_DOUBLE_EQ(curve.evaluate(0.37), 0.37);
    EXPECT_FALSE(threePoints(an::CurveInterpolation::Linear).isIdentity());
}

TEST(ToneCurveModelTest, InsertionKeepsInputOrderAndHoldsTheOutput)
{
    an::ui::ToneCurveModel curve;
    EXPECT_EQ(curve.insertPoint(0.5, 1.7), 1u);
    EXPECT_EQ(curve.insertPoint(0.25, -0.2), 1u);
    ASSERT_EQ(curve.size(), 4u);
    EXPECT_DOUBLE_EQ(curve.points()[1].input, 0.25);
    EXPECT_DOUBLE_EQ(curve.points()[1].output, 0.0);
    EXPECT_DOUBLE_EQ(curve.points()[2].input, 0.5);
    EXPECT_DOUBLE_EQ(curve.points()[2].output, 1.0);
}

// Two points cannot share an input, which the engine would refuse: a point inserted at an existing one's input moves
// it.
TEST(ToneCurveModelTest, APointAtAnExistingInputMovesThatPoint)
{
    an::ui::ToneCurveModel curve = threePoints(an::CurveInterpolation::Linear);
    EXPECT_EQ(curve.insertPoint(0.3, 0.9), 1u);
    ASSERT_EQ(curve.size(), 3u);
    EXPECT_DOUBLE_EQ(curve.points()[1].output, 0.9);
    EXPECT_NEAR(curve.evaluate(0.3), 0.9, 1e-12);
}

TEST(ToneCurveModelTest, MovingAPointKeepsItBetweenItsNeighbours)
{
    an::ui::ToneCurveModel curve = threePoints(an::CurveInterpolation::Linear);

    curve.updatePoint(1, 1.5, 0.4);
    EXPECT_DOUBLE_EQ(curve.points()[1].input, 1.0 - 0.0001);

    curve.updatePoint(1, -1.0, 0.4);
    EXPECT_DOUBLE_EQ(curve.points()[1].input, 0.0 + 0.0001);

    curve.updatePoint(1, 0.5, 2.0);
    EXPECT_DOUBLE_EQ(curve.points()[1].output, 1.0);

    curve.updatePoint(0, 0.2, 0.1);
    EXPECT_LT(curve.points()[0].input, curve.points()[1].input);

    curve.updatePoint(7, 0.5, 0.5);
    EXPECT_EQ(curve.size(), 3u);
}

// An inner point dragged past an inner neighbour stops a ten-thousandth short of it, on either side.
TEST(ToneCurveModelTest, AnInnerPointStopsShortOfAnInnerNeighbour)
{
    const an::ui::ToneCurveModel fourPoints({{0.0, 0.0}, {0.3, 0.5}, {0.6, 0.7}, {1.0, 1.0}},
                                            an::CurveInterpolation::Linear);

    an::ui::ToneCurveModel forward = fourPoints;
    forward.updatePoint(1, 0.8, 0.5);
    EXPECT_DOUBLE_EQ(forward.points()[1].input, 0.6 - 0.0001);

    an::ui::ToneCurveModel backward = fourPoints;
    backward.updatePoint(2, 0.1, 0.7);
    EXPECT_DOUBLE_EQ(backward.points()[2].input, 0.3 + 0.0001);
}

TEST(ToneCurveModelTest, AnEndPointCannotBeRemoved)
{
    an::ui::ToneCurveModel curve = threePoints(an::CurveInterpolation::Linear);
    curve.removePoint(0);
    curve.removePoint(2);
    EXPECT_EQ(curve.size(), 3u);
    curve.removePoint(1);
    EXPECT_EQ(curve.size(), 2u);
}

TEST(ToneCurveModelTest, TooFewPointsFallBackAsTheProductsCurvesAlwaysDid)
{
    EXPECT_EQ(
        an::ui::ToneCurveModel({{0.0, 0.0}, {1.0, 1.0}}, an::CurveInterpolation::NaturalCubic).effectiveInterpolation(),
        an::CurveInterpolation::Linear);
    EXPECT_EQ(threePoints(an::CurveInterpolation::Akima).effectiveInterpolation(),
              an::CurveInterpolation::NaturalCubic);
    EXPECT_EQ(threePoints(an::CurveInterpolation::Steffen).effectiveInterpolation(),
              an::CurveInterpolation::NaturalCubic);
    EXPECT_EQ(threePoints(an::CurveInterpolation::NaturalCubic).effectiveInterpolation(),
              an::CurveInterpolation::NaturalCubic);
    EXPECT_EQ(fivePoints(an::CurveInterpolation::Akima).effectiveInterpolation(), an::CurveInterpolation::Akima);
    EXPECT_EQ(fivePoints(an::CurveInterpolation::Steffen).effectiveInterpolation(), an::CurveInterpolation::Steffen);
}

TEST(ToneCurveModelTest, TheEngineEvaluatesTheCurveAndHoldsItAtTheEnds)
{
    const an::ui::ToneCurveModel linear = threePoints(an::CurveInterpolation::Linear);
    EXPECT_NEAR(linear.evaluate(0.15), 0.25, 1e-12);
    EXPECT_NEAR(linear.evaluate(0.3), 0.5, 1e-12);
    EXPECT_NEAR(linear.evaluate(0.65), 0.75, 1e-12);
    EXPECT_NEAR(linear.evaluate(-1.0), 0.0, 1e-12);
    EXPECT_NEAR(linear.evaluate(2.0), 1.0, 1e-12);

    // A curve the engine refuses - fewer than two points - evaluates to nothing rather than to a guess.
    EXPECT_EQ(an::ui::ToneCurveModel({{0.5, 0.5}}, an::CurveInterpolation::Linear).evaluate(0.5), 0.0);
}

TEST(ToneCurveModelTest, CurvesCompareByTheirPointsAndRule)
{
    EXPECT_EQ(threePoints(an::CurveInterpolation::Linear), threePoints(an::CurveInterpolation::Linear));
    EXPECT_FALSE(threePoints(an::CurveInterpolation::Linear) == threePoints(an::CurveInterpolation::Akima));
    EXPECT_FALSE(threePoints(an::CurveInterpolation::Linear) == an::ui::ToneCurveModel());
}

TEST(CurvesWidgetTest, EveryChangeReachesTheListenerWithTheCurve)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);
    RecordingListener listener;
    widget.setListener(&listener);

    widget.setCurve(threePoints(an::CurveInterpolation::Linear));
    widget.setCurveInterpolation(an::CurveInterpolation::Steffen);

    EXPECT_EQ(listener.updates, 2);
    EXPECT_EQ(listener.last.size(), 3u);
    EXPECT_EQ(listener.last.interpolation(), an::CurveInterpolation::Steffen);
    EXPECT_EQ(widget.getCurve(), listener.last);
    widget.setListener(nullptr);
}

TEST(CurvesWidgetTest, AWidgetNobodyGaveACurveHoldsTheIdentity)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);
    widget.resize(200, 100);
    EXPECT_TRUE(widget.curveIsIdentity());
    EXPECT_FALSE(widget.grab().toImage().isNull());
}

// A flat curve at one is drawn along the top row of the plot and nowhere near the bottom: the widget draws the curve
// the engine evaluates.
TEST(CurvesWidgetTest, TheWidgetDrawsTheCurveTheEngineEvaluates)
{
    QScrollArea parent;
    CurvesWidget widget(&parent);
    widget.setGradientAreaEnabled(false);
    widget.setGridEnabled(false);
    widget.setGuideLineEnabled(false);
    widget.setHistogramEnabled(false);
    widget.resize(200, 100);
    widget.setCurve(an::ui::ToneCurveModel({{0.0, 1.0}, {1.0, 1.0}}, an::CurveInterpolation::Linear));

    const QImage image = widget.grab().toImage();
    ASSERT_FALSE(image.isNull());
    const auto lit = [&image](int x, int fromRow, int toRow) {
        for (int y = fromRow; y <= toRow; ++y)
        {
            if (image.pixelColor(x, y).lightness() > 150)
            {
                return true;
            }
        }
        return false;
    };
    int litTop = 0;
    int litBottom = 0;
    for (int x = 10; x < 190; ++x)
    {
        litTop += lit(x, 0, 2) ? 1 : 0;
        litBottom += lit(x, 60, 99) ? 1 : 0;
    }
    EXPECT_GT(litTop, 150);
    EXPECT_EQ(litBottom, 0);
}

// A left click on the curve between two points adds one there and dragging it moves it; a right click on an inner point
// removes it, and one on an end point does not.
TEST(CurvesWidgetTest, ClickingTheCurveAddsAPointDraggingMovesItAndARightClickRemovesIt)
{
    ClickableCurves curves;
    RecordingListener listener;
    curves.widget.setListener(&listener);
    curves.widget.setCurve(an::ui::ToneCurveModel());

    curves.press(curves.at(0.5, 0.5), Qt::LeftButton);
    ASSERT_EQ(curves.widget.getCurve().size(), 3u);
    curves.move(curves.at(0.5, 0.8));
    curves.release(curves.at(0.5, 0.8), Qt::LeftButton);
    const an::ui::ToneCurveModel moved = curves.widget.getCurve();
    ASSERT_EQ(moved.size(), 3u);
    EXPECT_NEAR(moved.points()[1].input, 0.5, 0.01);
    EXPECT_NEAR(moved.points()[1].output, 0.8, 0.01);
    EXPECT_EQ(listener.last, moved);

    curves.press(curves.at(0.0, 0.0), Qt::RightButton);
    EXPECT_EQ(curves.widget.getCurve().size(), 3u);
    curves.press(curves.at(moved.points()[1].input, moved.points()[1].output), Qt::RightButton);
    EXPECT_EQ(curves.widget.getCurve().size(), 2u);
    curves.widget.setListener(nullptr);
}

// Each curve's application is recorded sample by sample over floating inputs; the curve the widget hands on - its
// cubic is the natural cubic, and the dialog opens on the identity - evaluated by the engine at every input sample is
// held to it by the record's own classification.
TEST(CurveRecordTest, TheWidgetsCurveReproducesTheRecordedCurves)
{
    struct CurveCase
    {
        const char* recordId;
        an::ui::ToneCurveModel curve;
    };
    const std::vector<CurveCase> cases{
        {"curves-transformation.default/scene-rgb-float32", an::ui::ToneCurveModel()},
        {"curves-transformation.cubic-three-nodes/scene-rgb-float32",
         threePoints(an::CurveInterpolation::NaturalCubic)},
        {"curves-transformation.cubic-three-nodes/scene-mono-float32",
         threePoints(an::CurveInterpolation::NaturalCubic)},
        {"curves-transformation.linear-three-nodes/scene-rgb-float32", threePoints(an::CurveInterpolation::Linear)},
        {"curves-transformation.akima-five-nodes/scene-rgb-float32", fivePoints(an::CurveInterpolation::Akima)},
        {"curves-transformation.steffen-five-nodes/scene-rgb-float32", fivePoints(an::CurveInterpolation::Steffen)},
    };

    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    ASSERT_TRUE(directory.has_value()) << error;
    const std::optional<std::vector<fixtures::Expectation>> records =
        fixtures::readExpectationFile(*directory / "behavior" / "curves-transformation.expectations", error);
    ASSERT_TRUE(records.has_value()) << error;

    for (const CurveCase& curveCase : cases)
    {
        const fixtures::Expectation* record = nullptr;
        for (const fixtures::Expectation& candidate : *records)
        {
            if (candidate.id == curveCase.recordId)
            {
                record = &candidate;
            }
        }
        ASSERT_NE(record, nullptr) << curveCase.recordId;
        EXPECT_EQ(record->provenance.producer, "AfterNightEngine") << curveCase.recordId;

        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record->inputName);
        ASSERT_NE(image, nullptr) << record->inputName;
        const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(input.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(*record, *image, *input))
            << curveCase.recordId << " was captured from another input than this one";

        fixtures::FixtureSamples output(input->width(), input->height(), input->channelCount(), input->sampleType());
        for (std::uint32_t channel = 0u; channel < input->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < input->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < input->width(); ++x)
                {
                    const double value = curveCase.curve.evaluate(input->value(x, y, channel));
                    output.setValue(x, y, channel, fixtures::quantizeSample(value, input->sampleType()));
                }
            }
        }
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(*record, output);
        EXPECT_TRUE(comparison.matched) << curveCase.recordId << ": " << comparison.reason << " ("
                                        << comparison.differingSamples << " samples, largest "
                                        << comparison.largestDifference << ")";
    }
}
