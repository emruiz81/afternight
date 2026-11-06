// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The curves transformation: what Apply writes for each recorded curve and channel, held to its records - a CIE
/// lightness recomposition after the same clamp; that Apply is the engine's curve, exact at every code of an eight- or
/// sixteen-bit image and within its table's stated error over a floating one; that every channel choice runs and a
/// stored channel's curve leaves the others alone; and that the identity curve leaves the image as it is; that every
/// interpolation the dialog offers is the engine's rule; and that a preview area shows exactly what Apply writes there.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "processes/CurvesTransformationProcess.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <QMouseEvent>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;

const std::vector<an::CurveControlPoint> kThreeNodes{{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}};
const std::vector<an::CurveControlPoint> kFiveNodes{{0.0, 0.0}, {0.2, 0.3}, {0.4, 0.55}, {0.7, 0.8}, {1.0, 1.0}};

/// One of the recorded curves, by the name its records carry.
struct RecordedCurve
{
    const char* mode;
    const std::vector<an::CurveControlPoint>* points;
    const char* interpolation;
    const char* channel = "RGB";
};

const RecordedCurve kRecordedCurves[] = {
    {.mode = "default", .points = nullptr, .interpolation = "Cubic"},
    {.mode = "cubic-three-nodes", .points = &kThreeNodes, .interpolation = "Cubic"},
    {.mode = "linear-three-nodes", .points = &kThreeNodes, .interpolation = "Linear"},
    {.mode = "akima-five-nodes", .points = &kFiveNodes, .interpolation = "Akima"},
    {.mode = "steffen-five-nodes", .points = &kFiveNodes, .interpolation = "Steffen"},
    {.mode = "cubic-three-nodes-red", .points = &kThreeNodes, .interpolation = "Cubic", .channel = "Red"},
    {.mode = "cubic-three-nodes-cie-l",
     .points = &kThreeNodes,
     .interpolation = "Cubic",
     .channel = "CIE L* (Lightness)"},
    {.mode = "cubic-three-nodes-hsv-value", .points = &kThreeNodes, .interpolation = "Cubic", .channel = "HSV Value"}};

/// Sets the dialog to a recorded curve: the interpolation chosen, then the curve's points in the widget.
void setCurve(CurvesTransformationProcess& process, const RecordedCurve& curve)
{
    choose(process, "interpolationSelector", QString::fromLatin1(curve.interpolation));
    if (child<QComboBox>(process, "channelSelector2")->count() > 1)
    {
        choose(process, "channelSelector2", QString::fromLatin1(curve.channel));
    }
    CurvesWidget* widget = curvesWidgetOf(process);
    ASSERT_NE(widget, nullptr);
    if (curve.points != nullptr)
    {
        widget->setCurve(an::ui::ToneCurveModel(*curve.points, widget->getCurve().interpolation()));
    }
}

[[nodiscard]] std::string inputOf(const fixtures::Expectation& record)
{
    return record.id.substr(record.id.find('/') + 1u);
}

[[nodiscard]] std::string modeOf(const fixtures::Expectation& record)
{
    const std::string::size_type dot = record.id.find('.');
    return record.id.substr(dot + 1u, record.id.find('/') - dot - 1u);
}

/// The samples of an Apply over a fixture input with a curve, in a process context of its own.
[[nodiscard]] std::optional<fixtures::FixtureSamples> applyOver(const std::string& input, const RecordedCurve& curve)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    EXPECT_NE(context, nullptr);
    if (context == nullptr)
    {
        return std::nullopt;
    }
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), input);
    if (!view)
    {
        return std::nullopt;
    }
    WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
    setCurve(*process, curve);
    EXPECT_TRUE(settleWindow(*process));
    process->applyProcess();
    EXPECT_TRUE(settleWindow(*process));
    EXPECT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u) << curve.mode;
    EXPECT_EQ(context->errorCount(), 0) << curve.mode;
    return samplesOf(view->document());
}

/// A CIE recomposition's precision against the record's after the same clamp.
constexpr double kCiePrecision = 5.0e-5;

/// Holds a CIE recomposition to its record: the record clamps every recomposed sample to the unit interval and the
/// engine keeps a floating one where the inverse conversion puts it, so after the same clamp the two agree within the
/// conversion's precision, and every sample further apart is one the engine left outside the interval.
void expectTheSameAfterTheClamp(const fixtures::FixtureSamples& result, const fixtures::FixtureSamples& recorded,
                                const std::string& id)
{
    ASSERT_EQ(result.channelCount(), recorded.channelCount()) << id;
    std::uint64_t outside = 0u;
    for (std::uint32_t channel = 0u; channel < result.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < result.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < result.width(); ++x)
            {
                const double value = result.value(x, y, channel);
                const double expected = recorded.value(x, y, channel);
                EXPECT_NEAR(std::clamp(value, 0.0, 1.0), expected, kCiePrecision) << id << ' ' << x << ',' << y;
                if (std::fabs(value - expected) > kCiePrecision)
                {
                    EXPECT_TRUE(value < 0.0 || value > 1.0) << id << ' ' << x << ',' << y << ": " << value;
                    ++outside;
                }
            }
        }
    }
    EXPECT_GT(outside, 0u) << id << ": the record differs only where the engine keeps a sample out of gamut";
}

/// The samples of a fixture input.
[[nodiscard]] fixtures::FixtureSamples inputSamples(const std::string& input)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples =
        fixtures::materializeFixture(*fixtures::fixtureNamed(input), error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples.has_value() ? std::move(*samples) : fixtures::FixtureSamples{};
}

/// Sends a widget a left-button mouse event at a point of it, as a person's hand does: a press, a move with the button
/// held, or the release.
void sendMouse(QWidget& widget, QEvent::Type type, const QPoint& at)
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, QPointF(at), QPointF(widget.mapToGlobal(at)), button, held, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

} // namespace

TEST(CurvesTransformationTest, ApplyCurvesEveryRecordedInputAsItsRecordHolds)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("curves-transformation");
    ASSERT_EQ(records.size(), 10u);
    for (const fixtures::Expectation& record : records)
    {
        const RecordedCurve* curve = nullptr;
        for (const RecordedCurve& candidate : kRecordedCurves)
        {
            if (modeOf(record) == candidate.mode)
            {
                curve = &candidate;
            }
        }
        ASSERT_NE(curve, nullptr) << record.id;
        const std::optional<fixtures::FixtureSamples> result = applyOver(inputOf(record), *curve);
        ASSERT_TRUE(result.has_value()) << record.id;
        if (std::string(curve->channel) == "CIE L* (Lightness)")
        {
            expectTheSameAfterTheClamp(*result, record.result, record.id);
            continue;
        }
        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
        EXPECT_TRUE(compared.matched) << record.id << ": " << compared.reason << ", " << compared.differingSamples
                                      << " samples differ, by up to " << compared.largestDifference;
    }
}

TEST(CurvesTransformationTest, ApplyIsTheCurveExactlyAtEveryIntegerCodeAndWithinItsTableOverAFloat)
{
    const an::ui::ToneCurveModel curve(kThreeNodes, an::CurveInterpolation::NaturalCubic);
    const RecordedCurve recorded{.mode = "cubic", .points = &kThreeNodes, .interpolation = "Cubic"};
    for (const char* input : {"scene-rgb-uint16", "scene-mono-uint8"})
    {
        SCOPED_TRACE(input);
        const fixtures::FixtureSamples original = inputSamples(input);
        const double fullScale = fixtures::highestOf(original.sampleType());
        const an::Result<an::CurveProgramPlan> plan = curvesProgramFor(
            curve, fullScale > 255.0 ? an::SampleType::UInt16 : an::SampleType::UInt8, original.channelCount());
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->approximation(), an::CurveApproximation::ExactAtResidentValues);
        const std::optional<fixtures::FixtureSamples> applied = applyOver(input, recorded);
        ASSERT_TRUE(applied.has_value());
        for (std::uint32_t channel = 0u; channel < original.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < original.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < original.width(); ++x)
                {
                    // The nearest code to the curve at this code, the table holding each entry in Float32.
                    const double exact = curve.evaluate(original.value(x, y, channel) / fullScale) * fullScale;
                    EXPECT_LE(std::fabs(applied->value(x, y, channel) - exact),
                              0.5 + fullScale * std::numeric_limits<float>::epsilon())
                        << x << ',' << y << ',' << channel;
                }
            }
        }
    }
    {
        const fixtures::FixtureSamples original = inputSamples("scene-rgb-float32");
        const an::Result<an::CurveProgramPlan> plan = curvesProgramFor(curve, an::SampleType::Float32, 3u);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->approximation(), an::CurveApproximation::SampledLinear);
        EXPECT_EQ(plan->tableEntries(), kCurvesTableEntries);
        EXPECT_LT(plan->maximumAbsoluteError(), 1.0e-6);
        const std::optional<fixtures::FixtureSamples> applied = applyOver("scene-rgb-float32", recorded);
        ASSERT_TRUE(applied.has_value());
        int compared = 0;
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < original.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < original.width(); ++x)
                {
                    const double value = original.value(x, y, channel);
                    if (value < 0.0 || value > 1.0)
                    {
                        continue;
                    }
                    ++compared;
                    // The table's error, and the Float32 the result is stored in.
                    EXPECT_NEAR(applied->value(x, y, channel), curve.evaluate(value),
                                plan->maximumAbsoluteError() + 1.0e-7)
                        << x << ',' << y << ',' << channel;
                }
            }
        }
        EXPECT_GT(compared, 0);
    }
}

// Every choice runs over a floating scene and over a sixteen-bit one described as sensor codes, as every integer file
// is read; a component of the second is taken from its samples as fractions of full scale.
TEST(CurvesTransformationTest, EveryChannelChoiceRunsAndAStoredChannelsCurveLeavesTheOthersAlone)
{
    QStringList choices;
    {
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
        WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
        auto* selector = child<QComboBox>(*process, "channelSelector2");
        ASSERT_NE(selector, nullptr);
        for (int item = 0; item < selector->count(); ++item)
        {
            if (!selector->itemText(item).isEmpty())
            {
                choices << selector->itemText(item);
            }
        }
    }
    ASSERT_GE(choices.size(), 15);
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16"})
    {
        const fixtures::FixtureSamples original = inputSamples(input);
        const double movement = fixtures::isFloating(original.sampleType()) ? 1.0e-3 : 65.0;
        for (const QString& choice : choices)
        {
            SCOPED_TRACE(std::string(input) + ", " + choice.toStdString());
            const std::string label = choice.toStdString();
            const RecordedCurve curve{
                .mode = "choice", .points = &kThreeNodes, .interpolation = "Cubic", .channel = label.c_str()};
            const std::optional<fixtures::FixtureSamples> applied = applyOver(input, curve);
            ASSERT_TRUE(applied.has_value());
            EXPECT_GT(largestDifference(*applied, original), movement) << "the curve moved the image";
            const int stored = label == "Red" ? 0 : label == "Green" ? 1 : label == "Blue" ? 2 : -1;
            if (stored < 0)
            {
                continue;
            }
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                if (static_cast<int>(channel) == stored)
                {
                    continue;
                }
                for (std::uint32_t y = 0u; y < original.height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < original.width(); ++x)
                    {
                        EXPECT_EQ(applied->value(x, y, channel), original.value(x, y, channel));
                    }
                }
            }
        }
    }
}

TEST(CurvesTransformationTest, TheIdentityCurveLeavesTheImageAsItIs)
{
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16", "scene-mono-int16"})
    {
        SCOPED_TRACE(input);
        const std::optional<fixtures::FixtureSamples> applied =
            applyOver(input, RecordedCurve{.mode = "identity", .points = nullptr, .interpolation = "Cubic"});
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(largestDifference(*applied, inputSamples(input)), 0.0);
    }
}

TEST(CurvesTransformationTest, APreviewAreaShowsExactlyWhatApplyWritesThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const char* channel : {"RGB", "CIE L* (Lightness)"})
    {
        SCOPED_TRACE(channel);
        ViewHandle view = viewOf(context->engineSession(), largeScene(fixtures::FixtureSampleType::UInt16));
        ASSERT_NE(view, nullptr);
        WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
        setCurve(*process,
                 RecordedCurve{.mode = "area", .points = &kThreeNodes, .interpolation = "Cubic", .channel = channel});
        ASSERT_TRUE(settleWindow(*process));
        openPreviewArea(*process);
        const QRect area = process->previewArea();
        ASSERT_LT(area.width(), 900);
        const std::optional<fixtures::FixtureSamples> previewed = samplesOf(process->previewView()->document());
        ASSERT_TRUE(previewed.has_value());

        process->applyProcess();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));
        const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(differingSamplesIn(*previewed, *applied, area), 0u);
        EXPECT_EQ(process->runTelemetry().previewsFailed, 0u);
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral(" | ")).toStdString();
    }
}

// A point of the curve taken with the left button and dragged is a drag, as a held slider's is: its changes are shown
// in interactive frames over the target reduced by the preview view's factor, which leave the preview's document as it
// was and which the curve editor's histogram does not count again; letting the point go asks at once for the settled
// frame over the whole target, which the editor counts and which is what the Apply writes, sample for sample.
TEST(CurvesTransformationTest, ADraggedPointIsShownInInteractiveFramesOverAReductionAndItsReleaseSettlesTheApply)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = viewOf(context->engineSession(), largeScene(fixtures::FixtureSampleType::Float32));
    ASSERT_NE(view, nullptr);
    WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
    EXPECT_EQ(process->previewScaling(), an::ui::PreviewScaling::Decimated);
    CurvesWidget* curves = curvesWidgetOf(*process);
    ASSERT_NE(curves, nullptr);
    curves->setCurve(
        an::ui::ToneCurveModel({{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}}, an::CurveInterpolation::NaturalCubic));
    process->show();
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(settleWindow(*process));
    preview->getViewWidget()->scaleImageView(0.25, true, true);
    ASSERT_TRUE(settleWindow(*process));
    const std::optional<an::ui::ViewPresentationSettings> painted =
        preview->getViewWidget()->framePresentationSettings();
    ASSERT_TRUE(painted.has_value());
    ASSERT_EQ(painted->proxyFactor, 4u);

    const ProcessRunTelemetry before = process->runTelemetry();
    const an::ui::HistogramCountTelemetry countedBefore = process->histogramTelemetry();
    const an::consumer::AssetRevision documentBefore = preview->document().asset()->revision();

    // The middle point taken and dragged up, held while its frames are shown.
    const QPoint taken = curves->pointPosition(1u);
    sendMouse(*curves, QEvent::MouseButtonPress, taken);
    for (int step = 1; step <= 3; ++step)
    {
        sendMouse(*curves, QEvent::MouseMove, taken - QPoint(0, 10 * step));
    }
    ASSERT_TRUE(pumpUntil(
        [&]() { return process->runTelemetry().interactivePreviewsCommitted > before.interactivePreviewsCommitted; }));
    const ProcessRunTelemetry held = process->runTelemetry();
    EXPECT_EQ(held.settledPreviewsCommitted, before.settledPreviewsCommitted) << "a held point's frame was settled";
    EXPECT_EQ(held.viewFramesCommitted - before.viewFramesCommitted,
              held.interactivePreviewsCommitted - before.interactivePreviewsCommitted)
        << "an interactive frame ran over the whole target";
    EXPECT_EQ(held.viewFrameFactor, 4u);
    EXPECT_EQ(preview->document().asset()->revision(), documentBefore)
        << "an interactive frame was published to the preview's document";
    EXPECT_EQ(process->histogramTelemetry().started, countedBefore.started)
        << "the curve editor counted its histogram again while the point was held";

    // Letting it go, right after a last move, asks for the settled frame there and then: the settle delay, which
    // settles a point held still, has not passed.
    sendMouse(*curves, QEvent::MouseMove, taken - QPoint(0, 40));
    const std::uint64_t requestedBeforeRelease = process->runTelemetry().previewsRequested;
    sendMouse(*curves, QEvent::MouseButtonRelease, curves->pointPosition(1u));
    EXPECT_EQ(process->runTelemetry().previewsRequested, requestedBeforeRelease + 1u)
        << "letting the point go asked for no settled frame";
    ASSERT_TRUE(settleWindow(*process));
    const ProcessRunTelemetry settled = process->runTelemetry();
    EXPECT_EQ(settled.settledPreviewsCommitted - before.settledPreviewsCommitted, 1u);
    EXPECT_EQ(settled.viewFramesCommitted, held.viewFramesCommitted) << "the settled frame ran over a reduction";
    EXPECT_NE(preview->document().asset()->revision(), documentBefore);
    EXPECT_GT(process->histogramTelemetry().started, countedBefore.started)
        << "the curve editor did not count the settled frame";
    EXPECT_GT(curves->getCurve().points()[1].output, 0.5);
    const std::optional<fixtures::FixtureSamples> previewed = samplesOf(preview->document());
    ASSERT_TRUE(previewed.has_value());

    process->applyProcess();
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(largestDifference(*previewed, *applied), 0.0) << "the settled frame is not the Apply";
    EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral(" | ")).toStdString();
}

// The dialog offers four rules under their texts and the engine's monotone cubic; each entry is the engine's rule of
// its name, the curve Apply writes.
TEST(CurvesTransformationTest, EveryInterpolationTheDialogOffersIsTheEnginesRule)
{
    const std::vector<std::pair<const char*, an::CurveInterpolation>> offered{
        {"Linear", an::CurveInterpolation::Linear},
        {"Cubic", an::CurveInterpolation::NaturalCubic},
        {"Akima", an::CurveInterpolation::Akima},
        {"Steffen", an::CurveInterpolation::Steffen},
        {"Monotone Cubic", an::CurveInterpolation::MonotoneCubic}};
    {
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
        WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
        auto* selector = child<QComboBox>(*process, "interpolationSelector");
        ASSERT_NE(selector, nullptr);
        ASSERT_EQ(selector->count(), static_cast<int>(offered.size()));
        for (int item = 0; item < selector->count(); ++item)
        {
            EXPECT_EQ(selector->itemText(item).toStdString(), offered[static_cast<std::size_t>(item)].first);
        }
        EXPECT_EQ(selector->currentText(), QStringLiteral("Cubic"));
    }
    const fixtures::FixtureSamples original = inputSamples("scene-rgb-float32");
    for (const auto& [label, rule] : offered)
    {
        SCOPED_TRACE(label);
        const an::ui::ToneCurveModel curve(kFiveNodes, rule);
        const an::Result<an::CurveProgramPlan> plan = curvesProgramFor(curve, an::SampleType::Float32, 3u);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const std::optional<fixtures::FixtureSamples> applied = applyOver(
            "scene-rgb-float32", RecordedCurve{.mode = "rule", .points = &kFiveNodes, .interpolation = label});
        ASSERT_TRUE(applied.has_value());
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < original.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < original.width(); ++x)
                {
                    const double value = original.value(x, y, channel);
                    if (value < 0.0 || value > 1.0)
                    {
                        continue;
                    }
                    const an::Result<double> expected =
                        an::evaluateCurve(kFiveNodes, rule, an::CurveDomainPolicy::Clamp, value);
                    ASSERT_TRUE(expected.hasValue());
                    EXPECT_NEAR(applied->value(x, y, channel), *expected, plan->maximumAbsoluteError() + 1.0e-7)
                        << x << ',' << y << ',' << channel;
                }
            }
        }
    }
}

// A successful Apply resets the curve to identity in the interpolation the dialog shows.
TEST(CurvesTransformationTest, TheIdentityCurveAfterAnApplyKeepsTheShownInterpolation)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    WindowHandle<CurvesTransformationProcess> process = openWindow<CurvesTransformationProcess>(*view);
    setCurve(*process, RecordedCurve{.mode = "reset", .points = &kFiveNodes, .interpolation = "Akima"});
    ASSERT_TRUE(settleWindow(*process));
    process->applyProcess();
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_TRUE(settleView(*view));
    ASSERT_EQ(process->runTelemetry().runsCommitted, 1u);
    pumpQuietly();
    CurvesWidget* widget = curvesWidgetOf(*process);
    ASSERT_NE(widget, nullptr);
    EXPECT_TRUE(widget->curveIsIdentity());
    EXPECT_EQ(child<QComboBox>(*process, "interpolationSelector")->currentText(), QStringLiteral("Akima"));
    EXPECT_EQ(widget->getCurve().interpolation(), an::CurveInterpolation::Akima);
}
