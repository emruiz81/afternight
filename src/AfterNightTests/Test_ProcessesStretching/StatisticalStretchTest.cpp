// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The statistical stretch: that Apply differs from its records only by the median it is decided from - the records
/// reproduced from a histogram estimate of it, the dialog's result exactly the engine's statistical stretch of the
/// image's working copy; that a preview area is the whole image's stretch, cropped, so it shows exactly what Apply
/// writes there, normalization included; the clipping figures; and the image it refuses.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "processes/StatisticalStretch.h"
#include "processes/StretchProcessSupport.h"

#include <AfterNightEngine/Algorithms/Pointwise/StatisticalStretch.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;

/// One of the dialog's recorded modes, by the name its records carry.
struct RecordedMode
{
    const char* mode;
    bool linked = false;
    bool normalize = false;
    bool applyCurves = false;
    bool noBlackClip = false;
};

constexpr RecordedMode kRecordedModes[] = {{.mode = "default"},
                                           {.mode = "linked", .linked = true},
                                           {.mode = "normalize-curves", .normalize = true, .applyCurves = true},
                                           {.mode = "no-black-clip", .noBlackClip = true}};

[[nodiscard]] const RecordedMode* modeNamed(const std::string& name)
{
    for (const RecordedMode& mode : kRecordedModes)
    {
        if (name == mode.mode)
        {
            return &mode;
        }
    }
    return nullptr;
}

/// The dialog's parameters for a mode, at its default values.
[[nodiscard]] StatisticalStretchParams paramsOf(const RecordedMode& mode)
{
    StatisticalStretchParams params;
    params.targetMedian = 0.25;
    params.blackPointSigma = 5.0;
    params.curvesBoost = 0.2;
    params.linked = mode.linked;
    params.normalize = mode.normalize;
    params.applyCurves = mode.applyCurves;
    params.noBlackClip = mode.noBlackClip;
    return params;
}

/// Sets the dialog to a recorded mode over its default values.
void setMode(StatisticalStretch& process, const RecordedMode& mode)
{
    setValue(process, "targetMedianValue", 0.25);
    setValue(process, "blackPointSigmaValue", 5.0);
    setValue(process, "curvesBoostValue", 0.2);
    setChecked(process, "linkedStretchCheck", mode.linked);
    setChecked(process, "normalizeImageCheck", mode.normalize);
    setChecked(process, "applyCurvesCheck", mode.applyCurves);
    setChecked(process, "noBlackClipCheck", mode.noBlackClip);
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

/// The stretched samples of an Apply over a fixture input in a mode, in a process context of its own.
[[nodiscard]] std::optional<fixtures::FixtureSamples> applyOver(const std::string& input, const RecordedMode& mode)
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
    WindowHandle<StatisticalStretch> process = openWindow<StatisticalStretch>(*view);
    setMode(*process, mode);
    EXPECT_TRUE(settleWindow(*process));
    process->applyProcess();
    EXPECT_TRUE(settleWindow(*process));
    EXPECT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u) << mode.mode;
    EXPECT_EQ(context->errorCount(), 0) << mode.mode;
    return samplesOf(view->document());
}

/// The engine's statistical stretch of a fixture image, called directly, from its own analysis - or from that analysis
/// with each group's median replaced by the records' histogram estimate of it.
[[nodiscard]] std::optional<fixtures::FixtureSamples> stretchedDirectly(const an::consumer::EngineSession& session,
                                                                        const fixtures::FixtureImage& input,
                                                                        const RecordedMode& mode, bool estimatedMedian)
{
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(input, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    const an::StatisticalStretchParameters parameters =
        statisticalStretchParametersOf(paramsOf(mode), samples->channelCount());
    // The records' median of a linked colour image is of every channel's samples together.
    std::vector<float> medians;
    const double fullScale =
        fixtures::isFloating(samples->sampleType()) ? 1.0 : fixtures::highestOf(samples->sampleType());
    const std::uint32_t groups = parameters.linked ? 1u : samples->channelCount();
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        std::vector<float> values;
        for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
        {
            if (!parameters.linked && channel != group)
            {
                continue;
            }
            for (std::uint32_t y = 0u; y < samples->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < samples->width(); ++x)
                {
                    values.push_back(static_cast<float>(samples->value(x, y, channel)) / static_cast<float>(fullScale));
                }
            }
        }
        medians.push_back(histogramEstimatedMedian(values));
    }
    return engineDirectly(session, input, [&](const an::ExecutionContext& execution, an::consumer::AnyImage& working) {
        return std::visit(
            [&](auto& typed) -> an::Status {
                using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                if constexpr (!std::is_floating_point_v<SampleT>)
                {
                    return an::Status::failure(an::ErrorCode::Internal, "the working image is not floating");
                }
                else
                {
                    const std::optional<an::ImageWriteAccess<SampleT>> access = typed.write();
                    const an::ImageView<SampleT>& view = access->view();
                    an::Result<an::StatisticalStretchAnalysis> analysis = an::analyzeImageStatisticalStretch(
                        execution, an::Image<SampleT>::kSampleType, view.data(), view.layout(), parameters.linked);
                    if (!analysis.hasValue())
                    {
                        return analysis.status();
                    }
                    if (estimatedMedian)
                    {
                        for (std::uint32_t channel = 0u; channel < analysis->channels.size(); ++channel)
                        {
                            analysis->channels[channel].median =
                                static_cast<double>(medians[(std::min)(channel, groups - 1u)]);
                        }
                    }
                    return an::stretchImageStatistically(execution, an::Image<SampleT>::kSampleType, view.data(),
                                                         view.layout(), view.data(), view.layout(), parameters,
                                                         &*analysis)
                        .status();
                }
            },
            working);
    });
}

/// Turns a window's preview and its preview area on, and answers what the area shows and the area.
[[nodiscard]] std::optional<std::pair<fixtures::FixtureSamples, QRect>> previewedArea(StatisticalStretch& process)
{
    openPreview(process);
    openPreviewArea(process);
    std::optional<fixtures::FixtureSamples> shown = samplesOf(process.previewView()->document());
    if (!shown.has_value())
    {
        return std::nullopt;
    }
    return std::make_pair(std::move(*shown), process.previewArea());
}

} // namespace

// The statistical stretch takes the image's exact median, where the records estimate it from a histogram with as many
// bins as samples between the extremes. Each record is the engine's stretch from that estimate, within the record's
// tolerance; the dialog's Apply is the engine's stretch from the exact median, exactly; and the two are as far apart as
// the estimate makes them.
TEST(StatisticalStretchTest, ApplyDiffersFromItsRecordsOnlyByTheMedianItIsDecidedFrom)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("statistical-stretch");
    ASSERT_EQ(records.size(), 6u);
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    for (const fixtures::Expectation& record : records)
    {
        const RecordedMode* mode = modeNamed(modeOf(record));
        ASSERT_NE(mode, nullptr) << record.id;
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(inputOf(record));
        ASSERT_NE(input, nullptr);

        const std::optional<fixtures::FixtureSamples> fromEstimate =
            stretchedDirectly(context->engineSession(), *input, *mode, true);
        ASSERT_TRUE(fromEstimate.has_value()) << record.id;
        const fixtures::ExpectationComparison reproduced = fixtures::compareWithExpectation(record, *fromEstimate);
        EXPECT_TRUE(reproduced.matched) << record.id << " from the estimated median: " << reproduced.reason;

        const std::optional<fixtures::FixtureSamples> fromExact =
            stretchedDirectly(context->engineSession(), *input, *mode, false);
        ASSERT_TRUE(fromExact.has_value());
        const std::optional<fixtures::FixtureSamples> applied = applyOver(input->name, *mode);
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(largestDifference(*applied, *fromExact), 0.0) << record.id;

        // How far the estimate moves the stretch on these 96-pixel scenes.
        const double bound = fixtures::isFloating(applied->sampleType()) ? 0.01 : 130.0;
        EXPECT_LE(largestDifference(*applied, record.result), bound) << record.id;
    }
}

TEST(StatisticalStretchTest, APreviewAreaIsTheWholeImagesStretchCroppedAndShowsExactlyWhatApplyWritesThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const fixtures::FixtureSampleType sampleType :
         {fixtures::FixtureSampleType::Float32, fixtures::FixtureSampleType::UInt16})
    {
        for (const bool normalize : {false, true})
        {
            const fixtures::FixtureImage scene = largeScene(sampleType);
            ViewHandle view = viewOf(context->engineSession(), scene);
            ASSERT_NE(view, nullptr);
            WindowHandle<StatisticalStretch> process = openWindow<StatisticalStretch>(*view);
            setMode(*process, RecordedMode{.mode = "area", .normalize = normalize, .applyCurves = normalize});
            const std::optional<std::pair<fixtures::FixtureSamples, QRect>> previewed = previewedArea(*process);
            ASSERT_TRUE(previewed.has_value());
            const QRect area = previewed->second;
            ASSERT_LT(area.width(), 900);

            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*view));
            const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
            ASSERT_TRUE(applied.has_value());
            EXPECT_EQ(differingSamplesIn(previewed->first, *applied, area), 0u) << "normalize " << normalize;
            EXPECT_EQ(process->runTelemetry().previewsFailed, 0u);
            EXPECT_EQ(context->errorCount(), 0);
        }
    }
}

// The area's own statistics would stretch it otherwise: the scene's gradient puts other extremes and another spread in
// the centred area than in the whole image, so the area stretched by itself - what a preview that measured its source
// would show - is not what Apply writes there.
TEST(StatisticalStretchTest, APreviewAreaStretchedByItsOwnStatisticsWouldNotBeWhatApplyWritesThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const fixtures::FixtureImage scene = largeScene(fixtures::FixtureSampleType::Float32);
    ViewHandle view = viewOf(context->engineSession(), scene);
    ASSERT_NE(view, nullptr);
    WindowHandle<StatisticalStretch> process = openWindow<StatisticalStretch>(*view);
    setMode(*process, RecordedMode{.mode = "area", .normalize = true, .applyCurves = true});
    const std::optional<std::pair<fixtures::FixtureSamples, QRect>> previewed = previewedArea(*process);
    ASSERT_TRUE(previewed.has_value());
    const QRect area = previewed->second;

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = view->document().read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::ImageAsset> crop = an::ui::cropProcessRegion(
        context->engineSession(), scope->engine(), scope->execution(), lease->image(), lease->validity(), area);
    ASSERT_TRUE(crop.hasValue()) << crop.status().message();
    an::Result<an::consumer::AssetReadLease> cropLease = crop->read();
    ASSERT_TRUE(cropLease.hasValue());
    an::Result<an::consumer::AnyImage> alone =
        an::ui::processes::makeStretchWorkingCopy(scope->engine(), scope->execution(), cropLease->image());
    ASSERT_TRUE(alone.hasValue());
    auto* floating = std::get_if<an::Image<float>>(&*alone);
    ASSERT_NE(floating, nullptr);
    {
        const std::optional<an::ImageWriteAccess<float>> access = floating->write();
        const an::Result<an::StatisticalStretchResult> stretched = an::stretchImageStatistically(
            scope->execution(), an::SampleType::Float32, access->view().data(), access->view().layout(),
            access->view().data(), access->view().layout(),
            statisticalStretchParametersOf(
                paramsOf(RecordedMode{.mode = "area", .normalize = true, .applyCurves = true}), 3u));
        ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();
    }
    const auto view_ = floating->view();
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (int y = 0; y < area.height(); ++y)
        {
            for (int x = 0; x < area.width(); ++x)
            {
                const double own =
                    *view_->sampleAt(channel, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u);
                const double shown = previewed->first.value(static_cast<std::uint32_t>(area.x() + x),
                                                            static_cast<std::uint32_t>(area.y() + y), channel);
                largest = (std::max)(largest, std::abs(own - shown));
            }
        }
    }
    EXPECT_GT(largest, 1.0e-2);
}

TEST(StatisticalStretchTest, TheClippingFiguresAreTheWholeImagesBlackPointsAndWhatTheyCut)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    ASSERT_NE(view, nullptr);
    WindowHandle<StatisticalStretch> process = openWindow<StatisticalStretch>(*view);
    setMode(*process, RecordedMode{.mode = "figures"});
    setValue(*process, "blackPointSigmaValue", 1.0);
    ASSERT_TRUE(settleWindow(*process));
    auto* label = child<QLabel>(*process, "clipStatsLabel");
    ASSERT_NE(label, nullptr);

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = view->document().read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<StatisticalStretchMeasurement> measured =
        measureStatisticalStretch(scope->engine(), scope->execution(), lease->image(), lease->validity(), false);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    const auto* working = std::get_if<an::Image<float>>(measured->working.get());
    ASSERT_NE(working, nullptr);
    const auto workingView = working->view();
    const an::Result<an::StatisticalStretchClipStatistics> clipping =
        an::computeStatisticalStretchClipStatistics(scope->execution(), an::SampleType::Float32, workingView->data(),
                                                    workingView->layout(), measured->analysis, 1.0, false);
    ASSERT_TRUE(clipping.hasValue()) << clipping.status().message();
    ASSERT_GT(clipping->clippedPixels, 0u) << "a black point one sigma below the median cuts the darkest pixels";
    const QString expected =
        QStringLiteral("Black clipping @ R=%1, G=%2, B=%3: %4/%5 pixels (%6%)")
            .arg(clipping->blackPoints[0], 0, 'f', 6)
            .arg(clipping->blackPoints[1], 0, 'f', 6)
            .arg(clipping->blackPoints[2], 0, 'f', 6)
            .arg(static_cast<qulonglong>(clipping->clippedPixels))
            .arg(static_cast<qulonglong>(clipping->totalPixels))
            .arg(100.0 * static_cast<double>(clipping->clippedPixels) / static_cast<double>(clipping->totalPixels), 0,
                 'f', 4);
    EXPECT_EQ(label->text(), expected);

    // Without black clipping the black points are each channel's minimum, and only samples at it are cut.
    setChecked(*process, "noBlackClipCheck", true);
    ASSERT_TRUE(settleWindow(*process));
    EXPECT_TRUE(label->text().startsWith(QStringLiteral("Black clipping disabled @ R=")))
        << label->text().toStdString();
}

TEST(StatisticalStretchTest, AnImageOfOtherThanOneOrThreeChannelsIsRefusedAndLeftAsItWas)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = an::tests::processes::makeDocument<float>(
        context->engineSession(), 8u, 6u, an::tests::processes::Channels::Rgba,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return 0.1f + 0.05f * static_cast<float>(x + y + channel);
        });
    ASSERT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("rgba", std::move(*document));
    ASSERT_TRUE(settleView(*view));
    WindowHandle<StatisticalStretch> process = openWindow<StatisticalStretch>(*view);
    ASSERT_TRUE(settleWindow(*process));

    process->applyProcess();
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
    // The dialog's previews are refused for the same reason, each with its own message.
    ASSERT_GE(context->errorCount(), 1);
    for (const QString& text : context->errorTexts)
    {
        EXPECT_TRUE(text.contains(QStringLiteral("a statistical stretch needs a mono or RGB source")))
            << text.toStdString();
    }
    EXPECT_FALSE(view->viewHistory()->hasUndo());
    EXPECT_NEAR(an::tests::processes::documentSample(view->document(), 3u, 2u, 3u), 0.1 + 0.05 * 8.0, 1.0e-6);
    EXPECT_NEAR(an::tests::processes::documentSample(view->document(), 0u, 0u, 0u), 0.1, 1.0e-6);
}
