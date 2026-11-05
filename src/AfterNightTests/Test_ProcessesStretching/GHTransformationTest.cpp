// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The generalized hyperbolic transformation: what Apply writes for each of its curves and colour modes, held to its
/// records - the CIE lightness and saturation within the colour conversion's stated precision, the screen transfer
/// reproduced exactly from the records' own estimates of its statistics; that Apply is the engine's stretch over the
/// dialog's whole local intensity range; that its screen transfer is decided from the whole image, so a preview area
/// shows exactly what Apply writes there; and that a picked point sets the symmetry point to its mean.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "processes/GHTransformationProcess.h"

#include <QLabel>
#include <QPoint>
#include <QPushButton>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;

/// One of the dialog's recorded settings, by the name its records carry.
struct RecordedSetting
{
    const char* mode;
    const char* transformation = "Generalized Hyperbolic";
    bool invert = false;
    const char* channel = "RGB";
    double stretchFactor = 2.0;
    double localIntensity = 1.5;
    double symmetryPoint = 0.1;
    double blackPoint = 0.0;
    bool linkedStf = false;
};

constexpr RecordedSetting kRecordedSettings[] = {
    {.mode = "default", .stretchFactor = 0.0, .localIntensity = 0.0, .symmetryPoint = 0.0},
    {.mode = "hyperbolic-factor-2-intensity-1.5-symmetry-0.1"},
    {.mode = "hyperbolic-inverse", .invert = true},
    {.mode = "lightness", .channel = "Lightness"},
    {.mode = "saturation", .channel = "Saturation"},
    {.mode = "colour", .channel = "Colour"},
    {.mode = "red-only", .channel = "Red"},
    {.mode = "arcsinh", .transformation = "Arcsinh"},
    {.mode = "histogram", .transformation = "Histogram Transformation"},
    {.mode = "linear", .transformation = "Linear Stretch", .blackPoint = 0.05},
    {.mode = "inversion", .transformation = "Image Inversion"},
    {.mode = "stf-linked",
     .transformation = "STF Transformation",
     .stretchFactor = 0.0,
     .localIntensity = 0.0,
     .symmetryPoint = 0.0,
     .linkedStf = true}};

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

/// The dialog's parameters for a recorded setting, as its controls state them.
[[nodiscard]] GHTransformationParams paramsOf(const RecordedSetting& setting)
{
    GHTransformationParams params;
    const std::string transformation = setting.transformation;
    params.transformation = transformation == "Arcsinh"                    ? GhtTransformation::Arcsinh
                            : transformation == "Histogram Transformation" ? GhtTransformation::Histogram
                            : transformation == "Linear Stretch"           ? GhtTransformation::Linear
                            : transformation == "Image Inversion"          ? GhtTransformation::Inversion
                            : transformation == "STF Transformation"       ? GhtTransformation::ScreenTransfer
                                                                           : GhtTransformation::GeneralizedHyperbolic;
    params.invert = setting.invert;
    params.stretchFactor = setting.stretchFactor;
    params.localIntensity = setting.localIntensity;
    params.symmetryPoint = setting.symmetryPoint;
    params.blackPoint = setting.blackPoint;
    params.linkedStf = setting.linkedStf;
    return params;
}

/// The colour modes whose records the CIE conversion holds within its stated precision: the records' XYZ inverse is a
/// printed matrix rather than the inverse of its forward one.
constexpr double kCiePrecision = 5.0e-5;

/// Sets the dialog to a recorded setting.
void setSetting(GHTransformationProcess& process, const RecordedSetting& setting)
{
    choose(process, "transformationTypeSelector", QString::fromLatin1(setting.transformation));
    if (child<QComboBox>(process, "channelSelector2")->count() > 1)
    {
        choose(process, "channelSelector2", QString::fromLatin1(setting.channel));
    }
    setValue(process, "stretchFactorValue", setting.stretchFactor);
    setValue(process, "localIntensityValue", setting.localIntensity);
    setValue(process, "protectShadowsValue", 0.0);
    setValue(process, "protectHighlightsValue", 1.0);
    setValue(process, "symmetryPointValue", setting.symmetryPoint);
    setValue(process, "blackPointValue", setting.blackPoint);
    setValue(process, "whitePointValue", 1.0);
    setChecked(process, "invert", setting.invert);
    setChecked(process, "linkedStf", setting.linkedStf);
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
    WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
    setSetting(*process, setting);
    EXPECT_TRUE(settleWindow(*process));
    process->applyProcess();
    EXPECT_TRUE(settleWindow(*process));
    EXPECT_TRUE(settleView(*view));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u) << setting.mode;
    EXPECT_EQ(context->errorCount(), 0) << setting.mode;
    return samplesOf(view->document());
}

/// The engine's stretch of a fixture input as the dialog's parameters ask, from stated statistics, called directly.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
stretchedDirectly(const an::consumer::EngineSession& session, const fixtures::FixtureImage& input,
                  const GHTransformationParams& params, const an::ui::processes::ScreenTransferStatistics* statistics)
{
    return engineDirectly(session, input, [&](const an::ExecutionContext& execution, an::consumer::AnyImage& working) {
        const std::optional<an::ImageDescriptor> descriptor =
            std::visit([](const auto& typed) { return typed.descriptor(); }, working);
        if (!descriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the working image holds nothing");
        }
        const an::Result<an::StretchImageRequest> request = ghtStretchRequestOf(params, *descriptor, statistics);
        if (!request.hasValue())
        {
            return request.status();
        }
        return stretchInPlace(execution, working, *request);
    });
}

/// The screen-transfer statistics the engine measures of a fixture input.
[[nodiscard]] std::vector<an::StretchChannelStatistics> exactStatistics(const an::consumer::EngineSession& session,
                                                                        const fixtures::FixtureImage& input,
                                                                        const fixtures::FixtureSamples& samples)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return {};
    }
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, input, samples);
    EXPECT_TRUE(asset.hasValue());
    if (!asset.hasValue())
    {
        return {};
    }
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return {};
    }
    an::Result<an::ui::processes::ScreenTransferStatistics> measured =
        an::ui::processes::measureScreenTransferStatistics(scope->engine(), scope->execution(), lease->image(),
                                                           lease->validity());
    EXPECT_TRUE(measured.hasValue()) << measured.status().message();
    return measured.hasValue() ? measured->channels : std::vector<an::StretchChannelStatistics>{};
}

} // namespace

TEST(GHTransformationTest, ApplyStretchesEveryRecordedInputAsItsRecordHolds)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("gh-transformation");
    ASSERT_EQ(records.size(), 14u);
    for (const fixtures::Expectation& record : records)
    {
        const RecordedSetting* setting = settingNamed(modeOf(record));
        ASSERT_NE(setting, nullptr) << record.id;
        if (setting->transformation == std::string("STF Transformation"))
        {
            continue;
        }
        const std::optional<fixtures::FixtureSamples> result = applyOver(inputOf(record), *setting);
        ASSERT_TRUE(result.has_value()) << record.id;
        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
        const std::string channel = setting->channel;
        if (channel == "Lightness" || channel == "Saturation")
        {
            EXPECT_LE(largestDifference(*result, record.result), kCiePrecision) << record.id;
            continue;
        }
        EXPECT_TRUE(compared.matched) << record.id << ": " << compared.reason << ", " << compared.differingSamples
                                      << " samples differ, by up to " << compared.largestDifference;
    }
}

// The screen transfer takes the image's exact median and median absolute deviation, where the records estimate both
// from a histogram with as many bins as samples, as the histogram transformation's records do. The record is the
// engine's transfer from those estimates, within its tolerance, and the dialog's result is the engine's transfer from
// the exact statistics, exactly.
TEST(GHTransformationTest, TheScreenTransferDiffersFromItsRecordOnlyByItsStatistics)
{
    const std::vector<fixtures::Expectation> records = behaviorRecords("gh-transformation");
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    int screenTransfers = 0;
    for (const fixtures::Expectation& record : records)
    {
        const RecordedSetting* setting = settingNamed(modeOf(record));
        ASSERT_NE(setting, nullptr) << record.id;
        if (setting->transformation != std::string("STF Transformation"))
        {
            continue;
        }
        ++screenTransfers;
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(inputOf(record));
        ASSERT_NE(input, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> inputSamples = fixtures::materializeFixture(*input, error);
        ASSERT_TRUE(inputSamples.has_value()) << error;
        const GHTransformationParams params = paramsOf(*setting);

        const an::ui::processes::ScreenTransferStatistics estimated{.channels = estimatedStatistics(*inputSamples)};
        const std::optional<fixtures::FixtureSamples> fromEstimates =
            stretchedDirectly(context->engineSession(), *input, params, &estimated);
        ASSERT_TRUE(fromEstimates.has_value()) << record.id;
        const fixtures::ExpectationComparison reproduced = fixtures::compareWithExpectation(record, *fromEstimates);
        EXPECT_TRUE(reproduced.matched) << record.id << " from the estimated statistics: " << reproduced.reason;

        const an::ui::processes::ScreenTransferStatistics exact{
            .channels = exactStatistics(context->engineSession(), *input, *inputSamples)};
        const std::optional<fixtures::FixtureSamples> fromExact =
            stretchedDirectly(context->engineSession(), *input, params, &exact);
        ASSERT_TRUE(fromExact.has_value());
        const std::optional<fixtures::FixtureSamples> applied = applyOver(input->name, *setting);
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(largestDifference(*applied, *fromExact), 0.0) << record.id;
        // How far the estimates move the stretch on these 96-pixel scenes: a few thousandths of full scale.
        EXPECT_LE(largestDifference(*applied, record.result), 0.008) << record.id;
    }
    EXPECT_EQ(screenTransfers, 1);
}

TEST(GHTransformationTest, ApplyIsTheEnginesStretchOverTheDialogsWholeLocalIntensityRange)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16"})
    {
        for (const double intensity : {-5.0, -1.0, 0.0, 1.5, 15.0})
        {
            SCOPED_TRACE(std::string(input) + " at local intensity " + std::to_string(intensity));
            const RecordedSetting setting{.mode = "range", .localIntensity = intensity};
            const std::optional<fixtures::FixtureSamples> expected =
                stretchedDirectly(context->engineSession(), *fixtures::fixtureNamed(input), paramsOf(setting), nullptr);
            ASSERT_TRUE(expected.has_value());
            const std::optional<fixtures::FixtureSamples> applied = applyOver(input, setting);
            ASSERT_TRUE(applied.has_value());
            EXPECT_EQ(largestDifference(*applied, *expected), 0.0);
            std::string error;
            const std::optional<fixtures::FixtureSamples> original =
                fixtures::materializeFixture(*fixtures::fixtureNamed(input), error);
            ASSERT_TRUE(original.has_value()) << error;
            EXPECT_GT(largestDifference(*applied, *original), 0.0) << "a factor of 2 stretches the image";
        }
    }
}

TEST(GHTransformationTest, AScreenTransferOfAPreviewAreaIsTheWholeImagesAndShowsWhatApplyWritesThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const bool linked : {true, false})
    {
        SCOPED_TRACE(linked ? "linked" : "unlinked");
        ViewHandle view = viewOf(context->engineSession(), largeScene(fixtures::FixtureSampleType::UInt16));
        ASSERT_NE(view, nullptr);
        WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
        setSetting(*process, RecordedSetting{.mode = "area",
                                             .transformation = "STF Transformation",
                                             .stretchFactor = 0.0,
                                             .localIntensity = 0.0,
                                             .symmetryPoint = 0.0,
                                             .linkedStf = linked});
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

TEST(GHTransformationTest, APickedPointSetsTheSymmetryPointToItsMean)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const char* input : {"scene-rgb-float32", "scene-mono-uint8"})
    {
        SCOPED_TRACE(input);
        ViewHandle view = fixtureView(context->engineSession(), input);
        ASSERT_NE(view, nullptr);
        WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
        ASSERT_TRUE(settleWindow(*process));
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples =
            fixtures::materializeFixture(*fixtures::fixtureNamed(input), error);
        ASSERT_TRUE(samples.has_value()) << error;
        const double fullScale =
            fixtures::isFloating(samples->sampleType()) ? 1.0 : fixtures::highestOf(samples->sampleType());
        for (const QPoint point : {QPoint(3, 2), QPoint(9, 6)})
        {
            double mean = 0.0;
            for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
            {
                mean += samples->value(static_cast<std::uint32_t>(point.x()), static_cast<std::uint32_t>(point.y()),
                                       channel) /
                        fullScale;
            }
            mean = std::clamp(mean / samples->channelCount(), 0.0, 1.0);
            process->onPointSelected(point);
            ASSERT_TRUE(settleWindow(*process));
            auto* symmetryPoint = child<QDoubleSpinBox>(*process, "symmetryPointValue");
            ASSERT_NE(symmetryPoint, nullptr);
            EXPECT_NEAR(symmetryPoint->value(), mean, 0.5e-4 + 1.0e-9) << point.x() << ',' << point.y();
        }
        // A point outside the image picks nothing.
        const double kept = child<QDoubleSpinBox>(*process, "symmetryPointValue")->value();
        process->onPointSelected(QPoint(400, 400));
        ASSERT_TRUE(settleWindow(*process));
        EXPECT_EQ(child<QDoubleSpinBox>(*process, "symmetryPointValue")->value(), kept);
        EXPECT_EQ(context->errorCount(), 0);
    }
}

// A stretch of one channel leaves the others as they were: a floating image's green above one and blue below zero are
// not held to the unit interval, and a blue sample holding no number neither stops the run nor changes: the channels
// it does not stretch are copied.
TEST(GHTransformationTest, AStretchOfOneChannelLeavesTheOthersExactlyAsTheyWere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = outOfRangeColourView(context->engineSession());
    ASSERT_NE(view, nullptr);
    const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
    setSetting(*process, RecordedSetting{.mode = "red", .channel = "Red"});
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
TEST(GHTransformationTest, AThirtyTwoBitImagesUntouchedCodesComeBackExactly)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = wideCodeView(context->engineSession());
    ASSERT_NE(view, nullptr);
    const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
    setSetting(*process, RecordedSetting{.mode = "red", .channel = "Red"});
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

// A symmetry-point picker armed for the generalized hyperbolic transformation stops when the transformation changes:
// its button is hidden then, and a click on the image or the histogram would still change a value the dialog no longer
// shows.
TEST(GHTransformationTest, AnArmedSymmetryPointPickerStopsWhenTheTransformationChanges)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    ASSERT_NE(view, nullptr);
    WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
    ASSERT_TRUE(settleWindow(*process));
    for (const char* picker : {"spFromImage", "spFromImageArea", "spFromHistogram"})
    {
        SCOPED_TRACE(picker);
        choose(*process, "transformationTypeSelector", QStringLiteral("Generalized Hyperbolic"));
        auto* button = child<QPushButton>(*process, picker);
        ASSERT_NE(button, nullptr);
        button->click();
        ASSERT_TRUE(button->isChecked()) << "the picker is armed";
        choose(*process, "transformationTypeSelector", QStringLiteral("Linear Stretch"));
        EXPECT_FALSE(button->isChecked()) << "the picker stops with the transformation it belonged to";
        EXPECT_TRUE(child<QLabel>(*process, "spText")->text().isEmpty());
    }
    ASSERT_TRUE(settleWindow(*process));
}

// The screen transfer of a thirty-two-bit image is measured from its double-precision working copy, and runs.
TEST(GHTransformationTest, TheScreenTransferOfAThirtyTwoBitImageRuns)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = wideCodeView(context->engineSession());
    ASSERT_NE(view, nullptr);
    const std::optional<fixtures::FixtureSamples> source = samplesOf(view->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle<GHTransformationProcess> process = openWindow<GHTransformationProcess>(*view);
    setSetting(*process, *settingNamed("stf-linked"));
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

// The original image's histogram counts its lightness and its saturation from a file's image - sensor codes, or a
// floating physical quantity, as the reader describes every file - read as the fractions of full scale the view shows,
// as it counts the same samples stated normalized.
TEST(GHTransformationTest, TheOriginalHistogramsComponentOfAFilesImageIsCountedAsTheViewShowsIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return 0.05 + 0.03 * static_cast<double>(x) + 0.02 * static_cast<double>(y) + 0.1 * static_cast<double>(c);
    };
    const auto componentOf =
        [&]<typename SampleT>(
            an::NumericDomain domain,
            an::ui::processes::HistogramComponent component) -> std::optional<an::ui::HistogramDisplayChannel> {
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session, 16u, 12u, an::tests::viewer::Channels::Rgb,
            [&value](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(value(x, y, c));
                }
                else
                {
                    return static_cast<SampleT>(std::lround(value(x, y, c) * 65535.0));
                }
            },
            domain);
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
        EXPECT_TRUE(asset.has_value() && scope.has_value());
        if (!asset.has_value() || !scope.has_value())
        {
            return std::nullopt;
        }
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        EXPECT_TRUE(lease.hasValue());
        if (!lease.hasValue())
        {
            return std::nullopt;
        }
        an::Result<an::ui::HistogramDisplayData> counted = an::ui::processes::countStretchHistogram(
            scope->engine(), scope->execution(), lease->image(), lease->validity(), an::ui::HistogramResolution::Bits8,
            component, {0.2126, 0.7152, 0.0722});
        EXPECT_TRUE(counted.hasValue()) << counted.status().message();
        if (!counted.hasValue())
        {
            return std::nullopt;
        }
        EXPECT_EQ(counted->channels.size(), 4u) << "the component is counted beside the channels";
        return counted->channels.size() == 4u ? std::optional(counted->channels[3]) : std::nullopt;
    };
    using an::ui::processes::HistogramComponent;
    for (const HistogramComponent component : {HistogramComponent::Lightness, HistogramComponent::Saturation})
    {
        SCOPED_TRACE(component == HistogramComponent::Lightness ? "lightness" : "saturation");
        const std::optional<an::ui::HistogramDisplayChannel> physical =
            componentOf.template operator()<float>(an::NumericDomain::PhysicalScaled, component);
        const std::optional<an::ui::HistogramDisplayChannel> floating =
            componentOf.template operator()<float>(an::NumericDomain::NormalizedLinear, component);
        const std::optional<an::ui::HistogramDisplayChannel> codes =
            componentOf.template operator()<std::uint16_t>(an::NumericDomain::RawSensorCode, component);
        const std::optional<an::ui::HistogramDisplayChannel> sixteen =
            componentOf.template operator()<std::uint16_t>(an::NumericDomain::NormalizedLinear, component);
        ASSERT_TRUE(physical.has_value() && floating.has_value() && codes.has_value() && sixteen.has_value());
        EXPECT_EQ(physical->bins, floating->bins) << "a floating physical quantity";
        EXPECT_EQ(codes->bins, sixteen->bins) << "sensor codes";
        EXPECT_EQ(physical->samples, floating->samples);
    }
}
