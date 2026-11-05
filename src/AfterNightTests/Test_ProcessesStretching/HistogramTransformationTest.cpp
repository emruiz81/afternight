// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The histogram transformation: what Apply writes for the dialog's transfer, held to its records; that its automatic
/// stretches differ from the records only by the statistics they are derived from - the records reproduced exactly from
/// their own estimates, the dialog's result exactly the engine's transfer from the image's exact statistics; that a
/// preview area is stretched by the whole image's statistics, so it shows exactly what Apply writes there; and the
/// clipped samples and preview histogram the dialog shows.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "processes/HistogramTransformationProcess.h"
#include "session/PreviewProxies.h"

#include <QLineEdit>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;

/// One of the dialog's recorded settings, by the name its records carry.
struct RecordedSetting
{
    const char* mode;
    double shadows = 0.0;
    double midtones = 0.5;
    double highlights = 1.0;
    const char* channel = "RGB";
    HistogramAutoStretch autoStretch = HistogramAutoStretch::None;
};

constexpr RecordedSetting kRecordedSettings[] = {
    {.mode = "default"},
    {.mode = "shadows-0.02-midtones-0.25-highlights-0.95", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95},
    {.mode = "red-only", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95, .channel = "Red"},
    {.mode = "autostretch-linked", .autoStretch = HistogramAutoStretch::Linked},
    {.mode = "autostretch-unlinked", .autoStretch = HistogramAutoStretch::Unlinked}};

[[nodiscard]] const RecordedSetting* settingNamed(const std::string& mode)
{
    for (const RecordedSetting& setting : kRecordedSettings)
    {
        if (mode == setting.mode)
        {
            return &setting;
        }
    }
    return nullptr;
}

/// Sets the dialog to a recorded setting: its transfer, or its automatic stretch's button, which the dialog offers
/// while its preview is shown.
void setSetting(HistogramTransformationProcess& process, const RecordedSetting& setting)
{
    if (setting.autoStretch != HistogramAutoStretch::None)
    {
        openPreview(process);
        auto* button = child<QAbstractButton>(
            process, setting.autoStretch == HistogramAutoStretch::Linked ? "autostretchLinked" : "autostretchUnlinked");
        ASSERT_NE(button, nullptr);
        ASSERT_TRUE(button->isEnabled());
        button->click();
        return;
    }
    if (child<QComboBox>(process, "channelSelector2")->count() > 1)
    {
        choose(process, "channelSelector2", QString::fromLatin1(setting.channel));
    }
    setValue(process, "shadowsValue", setting.shadows);
    setValue(process, "midtonesValue", setting.midtones);
    setValue(process, "highlightsValue", setting.highlights);
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

/// The samples of an Apply over a fixture input with a setting, in a process context of its own.
[[nodiscard]] std::optional<fixtures::FixtureSamples> applyOver(const std::string& input,
                                                                const RecordedSetting& setting)
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
    WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
    setSetting(*process, setting);
    EXPECT_TRUE(settleWindow(*process));
    process->applyProcess();
    EXPECT_TRUE(settleWindow(*process));
    EXPECT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u) << setting.mode;
    EXPECT_EQ(context->errorCount(), 0) << setting.mode;
    return samplesOf(view->document());
}

/// The engine's screen transfer of a fixture input from stated statistics, called directly.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
transferredDirectly(const an::consumer::EngineSession& session, const fixtures::FixtureImage& input,
                    const RecordedSetting& setting, const std::vector<an::StretchChannelStatistics>& statistics)
{
    HistogramTransformationParams params;
    params.autoStretch = setting.autoStretch;
    params.shadows = setting.shadows;
    params.midtones = setting.midtones;
    params.highlights = setting.highlights;
    const an::ui::processes::ScreenTransferStatistics measured{.channels = statistics};
    const an::Result<HistogramTransfer> transfer = histogramTransferOf(params, &measured);
    EXPECT_TRUE(transfer.hasValue()) << transfer.status().message();
    if (!transfer.hasValue())
    {
        return std::nullopt;
    }
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::ScreenTransfer;
    request.stretch.screenTransfer = transfer->channels;
    request.channelEnabled = transfer->changed;
    return engineDirectly(session, input, [&](const an::ExecutionContext& execution, an::consumer::AnyImage& working) {
        return stretchInPlace(execution, working, request);
    });
}

} // namespace

TEST(HistogramTransformationTest, ApplyTransformsEveryRecordedInputAsItsRecordHolds)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("histogram-transformation");
    ASSERT_EQ(records.size(), 8u);
    for (const fixtures::Expectation& record : records)
    {
        const RecordedSetting* setting = settingNamed(modeOf(record));
        ASSERT_NE(setting, nullptr) << record.id;
        if (setting->autoStretch != HistogramAutoStretch::None)
        {
            continue;
        }
        const std::optional<fixtures::FixtureSamples> result = applyOver(inputOf(record), *setting);
        ASSERT_TRUE(result.has_value()) << record.id;
        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
        EXPECT_TRUE(compared.matched) << record.id << ": " << compared.reason;
    }
}

// The automatic stretch takes the image's exact median and median absolute deviation, where the records estimate both
// from a histogram with as many bins as samples. The records are the engine's transfer from those estimates, within
// their tolerance, and the dialog's result is the engine's transfer from the exact statistics, exactly; the two results
// are as far apart as the estimates make them.
TEST(HistogramTransformationTest, TheAutomaticStretchDiffersFromItsRecordsOnlyByItsStatistics)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("histogram-transformation");
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    int automatic = 0;
    for (const fixtures::Expectation& record : records)
    {
        const RecordedSetting* setting = settingNamed(modeOf(record));
        ASSERT_NE(setting, nullptr) << record.id;
        if (setting->autoStretch == HistogramAutoStretch::None)
        {
            continue;
        }
        ++automatic;
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(inputOf(record));
        ASSERT_NE(input, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> inputSamples = fixtures::materializeFixture(*input, error);
        ASSERT_TRUE(inputSamples.has_value()) << error;

        const std::optional<fixtures::FixtureSamples> fromEstimates =
            transferredDirectly(context->engineSession(), *input, *setting, estimatedStatistics(*inputSamples));
        ASSERT_TRUE(fromEstimates.has_value()) << record.id;
        const fixtures::ExpectationComparison reproduced = fixtures::compareWithExpectation(record, *fromEstimates);
        EXPECT_TRUE(reproduced.matched) << record.id << " from the estimated statistics: " << reproduced.reason;

        std::vector<an::StretchChannelStatistics> exact;
        {
            std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
            ASSERT_TRUE(scope.has_value());
            an::Result<an::consumer::ImageAsset> asset =
                fixtures::engine::makeImageAsset(context->engineSession(), *input, *inputSamples);
            ASSERT_TRUE(asset.hasValue());
            an::Result<an::consumer::AssetReadLease> lease = asset->read();
            ASSERT_TRUE(lease.hasValue());
            an::Result<an::ui::processes::ScreenTransferStatistics> measured =
                an::ui::processes::measureScreenTransferStatistics(scope->engine(), scope->execution(), lease->image(),
                                                                   lease->validity());
            ASSERT_TRUE(measured.hasValue()) << measured.status().message();
            exact = measured->channels;
        }
        const std::optional<fixtures::FixtureSamples> fromExact =
            transferredDirectly(context->engineSession(), *input, *setting, exact);
        ASSERT_TRUE(fromExact.has_value());
        const std::optional<fixtures::FixtureSamples> applied = applyOver(input->name, *setting);
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(largestDifference(*applied, *fromExact), 0.0) << record.id;

        // How far the estimates move the stretch on these 96-pixel scenes: a few thousandths of full scale.
        const double bound = fixtures::isFloating(inputSamples->sampleType()) ? 0.008 : 72.0;
        EXPECT_LE(largestDifference(*applied, record.result), bound) << record.id;
    }
    EXPECT_EQ(automatic, 3);
}

TEST(HistogramTransformationTest, ApplyIsTheEnginesScreenTransferOfTheImagesWorkingCopy)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const fixtures::FixtureImage* input = fixtures::fixtureNamed("scene-rgb-uint16");
    ASSERT_NE(input, nullptr);
    const RecordedSetting setting{
        .mode = "manual", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95, .channel = "Green"};
    const std::optional<fixtures::FixtureSamples> expected =
        transferredDirectly(context->engineSession(), *input, setting, {});
    ASSERT_TRUE(expected.has_value());
    const std::optional<fixtures::FixtureSamples> applied = applyOver(input->name, setting);
    ASSERT_TRUE(applied.has_value());
    // Green alone: the engine was asked for every channel, so only green is compared, and red and blue are the
    // source's.
    std::string error;
    const std::optional<fixtures::FixtureSamples> original = fixtures::materializeFixture(*input, error);
    ASSERT_TRUE(original.has_value()) << error;
    for (std::uint32_t y = 0u; y < applied->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < applied->width(); ++x)
        {
            EXPECT_EQ(applied->value(x, y, 0u), original->value(x, y, 0u));
            EXPECT_EQ(applied->value(x, y, 2u), original->value(x, y, 2u));
        }
    }
    HistogramTransformationParams params;
    params.shadows = 0.02;
    params.midtones = 0.25;
    params.highlights = 0.95;
    params.channel = an::ChannelScope::Green;
    const an::Result<HistogramTransfer> transfer = histogramTransferOf(params, nullptr);
    ASSERT_TRUE(transfer.hasValue());
    EXPECT_EQ(transfer->changed, (std::array<bool, 4u>{false, true, false, false}));
    for (std::uint32_t y = 0u; y < applied->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < applied->width(); ++x)
        {
            const an::Result<double> transferred =
                an::evaluateMidtonesTransfer(original->value(x, y, 1u) / 65535.0, transfer->channels[1]);
            ASSERT_TRUE(transferred.hasValue());
            EXPECT_NEAR(applied->value(x, y, 1u), *transferred * 65535.0, 1.0) << x << ',' << y;
        }
    }
}

TEST(HistogramTransformationTest, AnAutomaticStretchOfAPreviewAreaIsTheWholeImagesAndShowsWhatApplyWritesThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const HistogramAutoStretch stretch : {HistogramAutoStretch::Linked, HistogramAutoStretch::Unlinked})
    {
        ViewHandle view = viewOf(context->engineSession(), largeScene(fixtures::FixtureSampleType::UInt16));
        ASSERT_NE(view, nullptr);
        WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
        setSetting(*process, RecordedSetting{.mode = "area", .autoStretch = stretch});
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
        EXPECT_EQ(context->errorCount(), 0);
    }
}

// A run over a view of the target - the target reduced, or a part of it, for an interactive frame - is stretched by the
// whole target's statistics, as a preview area's is: the reduction's own are narrowed by its averaging, and its
// revision is its own, so nothing measured of it is ever filed under the target's. A run over the target itself takes
// its own.
TEST(HistogramTransformationTest, ARunOverAViewOfTheTargetTakesTheWholeTargetsStatistics)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const an::consumer::EngineSession& session = context->engineSession();
    // A noisy frame: reduced by four, every block's mean narrows the spread its median absolute deviation measures.
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        session, 256u, 192u, an::tests::viewer::Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const std::uint32_t hashed = (x * 2654435761u) ^ (y * 2246822519u) ^ (c * 3266489917u);
            return 0.2f + 0.1f * static_cast<float>(hashed % 1000u) / 1000.0f;
        });
    ASSERT_TRUE(document.has_value());
    const std::shared_ptr<const an::consumer::ImageAsset> target = document->asset();
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    ASSERT_TRUE(scope.hasValue());
    an::Result<an::consumer::AssetReadLease> lease = target->read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::ImageAsset> reduced =
        an::ui::reduceForPreview(session, scope->engine(), scope->execution(), *lease, 4u);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    an::Result<an::consumer::AssetReadLease> reducedLease = reduced->read();
    ASSERT_TRUE(reducedLease.hasValue());
    const an::Result<an::ui::processes::ScreenTransferStatistics> whole =
        an::ui::processes::measureScreenTransferStatistics(scope->engine(), scope->execution(), lease->image(),
                                                           lease->validity());
    const an::Result<an::ui::processes::ScreenTransferStatistics> narrowed =
        an::ui::processes::measureScreenTransferStatistics(scope->engine(), scope->execution(), reducedLease->image(),
                                                           reducedLease->validity());
    ASSERT_TRUE(whole.hasValue() && narrowed.hasValue());
    ASSERT_NE(whole->channels, narrowed->channels) << "the reduction measures as the target does";

    an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics> cache;
    an::ui::ProcessRun overReduction{.kind = an::ui::ProcessRunKind::Preview,
                                     .frameKind = an::ui::PreviewFrameKind::Interactive};
    overReduction.proxyFactor = 4u;
    const an::Result<std::shared_ptr<const an::ui::processes::ScreenTransferStatistics>> taken =
        an::ui::processes::screenTransferStatisticsFor(scope->engine(), scope->execution(),
                                                       an::ui::ProcessSource::of(*reducedLease), overReduction, QRect(),
                                                       target, cache);
    ASSERT_TRUE(taken.hasValue()) << taken.status().message();
    EXPECT_EQ((*taken)->channels, whole->channels) << "a run over a reduction was stretched by the reduction";
    EXPECT_NE(cache.find(target->revision()), nullptr) << "the target's statistics are kept for its revision";
    EXPECT_EQ(cache.find(reduced->revision()), nullptr);

    an::ui::ProcessRun overRegion{.kind = an::ui::ProcessRunKind::Preview,
                                  .frameKind = an::ui::PreviewFrameKind::Interactive};
    overRegion.partOfTarget = true;
    const an::Result<std::shared_ptr<const an::ui::processes::ScreenTransferStatistics>> region =
        an::ui::processes::screenTransferStatisticsFor(scope->engine(), scope->execution(),
                                                       an::ui::ProcessSource::of(*reducedLease), overRegion, QRect(),
                                                       target, cache);
    ASSERT_TRUE(region.hasValue());
    EXPECT_EQ((*region)->channels, whole->channels);

    const an::ui::ProcessRun overItself{.kind = an::ui::ProcessRunKind::Preview};
    const an::Result<std::shared_ptr<const an::ui::processes::ScreenTransferStatistics>> own =
        an::ui::processes::screenTransferStatisticsFor(scope->engine(), scope->execution(),
                                                       an::ui::ProcessSource::of(*reducedLease), overItself, QRect(),
                                                       target, cache);
    ASSERT_TRUE(own.hasValue());
    EXPECT_EQ((*own)->channels, narrowed->channels) << "a run over its own source took the target's statistics";
}

TEST(HistogramTransformationTest, TheDialogShowsWhatItsTransferClipsAndTheHistogramItLeaves)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-mono-float32");
    ASSERT_NE(view, nullptr);
    WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
    ASSERT_TRUE(settleWindow(*process));
    auto* shadowsInfo = child<QLineEdit>(*process, "shadowsInfo");
    auto* highlightsInfo = child<QLineEdit>(*process, "highlightsInfo");
    ASSERT_NE(shadowsInfo, nullptr);
    ASSERT_NE(highlightsInfo, nullptr);
    EXPECT_EQ(shadowsInfo->text().toStdString(), "0 - 0.000000%");
    EXPECT_EQ(highlightsInfo->text().toStdString(), "0 - 0.000000%");

    // Shadows above the darkest samples clip them: the count is of the samples in bins below the shadows point's, at
    // the dialog's resolution, a share of the image's 96.
    setValue(*process, "shadowsValue", 0.2);
    ASSERT_TRUE(settleWindow(*process));
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples =
        fixtures::materializeFixture(*fixtures::fixtureNamed("scene-mono-float32"), error);
    ASSERT_TRUE(samples.has_value()) << error;
    std::uint64_t below = 0u;
    const std::size_t threshold = static_cast<std::size_t>(std::llround(0.2 * 255.0));
    for (std::uint32_t y = 0u; y < samples->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples->width(); ++x)
        {
            const double value = std::clamp(samples->value(x, y, 0u), 0.0, 1.0);
            const std::size_t bin = value >= 1.0 ? 255u : static_cast<std::size_t>(value * 255.0);
            below += bin < threshold ? 1u : 0u;
        }
    }
    ASSERT_GT(below, 0u);
    EXPECT_EQ(shadowsInfo->text(),
              QStringLiteral("%1 - %2%").arg(below).arg(static_cast<double>(below) * 100.0 / 96.0, 0, 'f', 6));
}

// A stretch of one channel leaves the others as they were: a floating image's green above one and blue below zero are
// not held to the unit interval, and a blue sample holding no number neither stops the run nor changes: the channels
// it does not stretch are copied.
TEST(HistogramTransformationTest, AStretchOfOneChannelLeavesTheOthersExactlyAsTheyWere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = outOfRangeColourView(context->engineSession());
    ASSERT_NE(view, nullptr);
    const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
    setSetting(*process,
               RecordedSetting{.mode = "red", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95, .channel = "Red"});
    ASSERT_TRUE(settleWindow(*process));
    process->applyProcess();
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_TRUE(settleView(*view));
    // The Apply of the red channel runs and commits; the dialog then returns to every channel, and its preview of the
    // blue channel refuses the sample holding no number, as a stretch of a channel holding one does.
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(process->runTelemetry().runsFailed, 0u);
    const std::optional<fixtures::FixtureSamples> result = samplesOf(view->document());
    ASSERT_TRUE(result.has_value());
    expectRedAloneChanged(*source, *result);
}

// A thirty-two-bit image is stretched in double precision, so the samples a stretch leaves alone come back as the
// codes they were.
TEST(HistogramTransformationTest, AThirtyTwoBitImagesUntouchedCodesComeBackExactly)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = wideCodeView(context->engineSession());
    ASSERT_NE(view, nullptr);
    const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
    setSetting(*process,
               RecordedSetting{.mode = "red", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95, .channel = "Red"});
    ASSERT_TRUE(settleWindow(*process));
    process->applyProcess();
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> result = samplesOf(view->document());
    ASSERT_TRUE(result.has_value());
    expectRedAloneChanged(*source, *result);
}

// An automatic stretch of a thirty-two-bit image is measured from its double-precision working copy, and runs.
TEST(HistogramTransformationTest, AnAutomaticStretchOfAThirtyTwoBitImageRuns)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const HistogramAutoStretch stretch : {HistogramAutoStretch::Linked, HistogramAutoStretch::Unlinked})
    {
        ViewHandle view = wideCodeView(context->engineSession());
        ASSERT_NE(view, nullptr);
        const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
        ASSERT_TRUE(source.has_value());
        WindowHandle<HistogramTransformationProcess> process = openWindow<HistogramTransformationProcess>(*view);
        setSetting(*process, RecordedSetting{.mode = "automatic", .autoStretch = stretch});
        ASSERT_TRUE(settleWindow(*process));
        process->applyProcess();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));
        EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
        EXPECT_EQ(process->runTelemetry().runsFailed, 0u);
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> result = samplesOf(view->document());
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(largestDifference(*source, *result), 0.0) << "the image is stretched";
    }
}
