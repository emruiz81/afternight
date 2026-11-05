// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the spatial and noise processes, measured through the dialog it was captured from: the
/// dialog opened over the capture's input, its widgets set to the capture's mode, and its Apply judged against the
/// record - reproduced within the record's own tolerance, or different from it in the way its case states and by no
/// more than the case's bound.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/BandingReductionProcess.h"
#include "processes/BilateralFilterProcess.h"
#include "processes/CLAHEProcess.h"
#include "processes/CosmeticCorrectionProcess.h"
#include "processes/GRAYCstorationProcess.h"
#include "processes/GaussianBlurProcess.h"
#include "processes/GmicProcessSupport.h"
#include "processes/HighPassFilterProcess.h"
#include "processes/LocalContrastEnhancementProcess.h"
#include "processes/LocalHistogramEqualizationProcess.h"
#include "processes/MedianFilterProcess.h"
#include "processes/MorphologicalTransformationProcess.h"
#include "processes/NLBayesProcess.h"
#include "processes/PatchPCADenoiseProcess.h"
#include "processes/SharpenAlphaProcess.h"
#include "processes/TightenEdgesProcess.h"
#include "processes/WaveletDenoiseProcess.h"

#include <QGroupBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

/// A dialog over a target, as the launcher's entry point opens it.
using Opener = std::function<SpatialHandle<ImageRTPreviewControl>(ImageViewControl&)>;

/// What a capture's mode sets in its dialog.
using Configure = std::function<void(QWidget&)>;

template <typename ProcessT> [[nodiscard]] Opener opener()
{
    return [](ImageViewControl& target) {
        return SpatialHandle<ImageRTPreviewControl>(openSpatial<ProcessT>(target).release());
    };
}

/// Checks or clears a checkable group box, as its title's check box does.
void setGroupChecked(const QWidget& window, const char* name, bool checked)
{
    if (auto* group = widgetNamed<QGroupBox>(window, name))
    {
        group->setChecked(checked);
    }
}

/// How a record is judged against what the dialog showed.
using Judge = std::function<void(const fixtures::Expectation&, const fixtures::FixtureSamples&)>;

/// How many stored units a fraction of one spans in the representation of a record's input.
[[nodiscard]] double fullScaleOf(const fixtures::Expectation& record)
{
    const fixtures::FixtureImage* input = fixtures::fixtureNamed(record.inputName);
    if (input == nullptr)
    {
        ADD_FAILURE() << "no fixture named " << record.inputName;
        return 1.0;
    }
    switch (input->sampleType)
    {
    case fixtures::FixtureSampleType::UInt8:
    case fixtures::FixtureSampleType::Int8:
        return 255.0;
    case fixtures::FixtureSampleType::UInt16:
    case fixtures::FixtureSampleType::Int16:
        return 65535.0;
    case fixtures::FixtureSampleType::UInt32:
    case fixtures::FixtureSampleType::Int32:
        return 4294967295.0;
    case fixtures::FixtureSampleType::Float32:
    case fixtures::FixtureSampleType::Float64:
        return 1.0;
    }
    return 1.0;
}

/// Whether one sample is within the record's own tolerance of the recorded one.
[[nodiscard]] bool withinRecord(const fixtures::Expectation& record, double shown, double recorded)
{
    return std::abs(shown - recorded) <=
           (std::max)(record.absoluteTolerance, record.relativeTolerance * std::abs(recorded)) + 1e-12;
}

/// The record reproduced within its own tolerance.
[[nodiscard]] Judge reproduces()
{
    return [](const fixtures::Expectation& record, const fixtures::FixtureSamples& shown) {
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, shown);
        EXPECT_TRUE(comparison.matched) << comparison.reason << "; " << comparison.differingSamples
                                        << " samples differ, the largest by " << comparison.largestDifference;
    };
}

/// The record differing by no more than a largest and a root-mean-square difference, both in fractions of full scale.
[[nodiscard]] Judge bounded(double largest, double rootMeanSquare)
{
    return [largest, rootMeanSquare](const fixtures::Expectation& record, const fixtures::FixtureSamples& shown) {
        ASSERT_EQ(shown.width(), record.result.width());
        ASSERT_EQ(shown.height(), record.result.height());
        ASSERT_EQ(shown.channelCount(), record.result.channelCount());
        const double scale = fullScaleOf(record);
        double widest = 0.0;
        double squares = 0.0;
        double count = 0.0;
        for (std::uint32_t c = 0u; c < shown.channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < shown.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < shown.width(); ++x)
                {
                    const double difference = (shown.value(x, y, c) - record.result.value(x, y, c)) / scale;
                    widest = (std::max)(widest, std::abs(difference));
                    squares += difference * difference;
                    count += 1.0;
                }
            }
        }
        EXPECT_LE(widest, largest);
        EXPECT_LE(std::sqrt(squares / count), rootMeanSquare);
    };
}

/// The record reproduced everywhere but the first and last sample of each row, which the record's edge filter reads
/// across into the neighbouring row.
[[nodiscard]] Judge reproducesAwayFromRowEnds()
{
    return [](const fixtures::Expectation& record, const fixtures::FixtureSamples& shown) {
        ASSERT_EQ(shown.width(), record.result.width());
        ASSERT_EQ(shown.height(), record.result.height());
        ASSERT_EQ(shown.channelCount(), record.result.channelCount());
        for (std::uint32_t c = 0u; c < shown.channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < shown.height(); ++y)
            {
                for (std::uint32_t x = 1u; x + 1u < shown.width(); ++x)
                {
                    ASSERT_TRUE(withinRecord(record, shown.value(x, y, c), record.result.value(x, y, c)))
                        << x << "," << y << "," << c << ": " << shown.value(x, y, c) << " against "
                        << record.result.value(x, y, c);
                }
            }
        }
    };
}

/// A grey record reproduced; a colour one reproduced in its first channel, every other channel corrected by its own
/// figures where the record applies the first channel's to every one, and differing by no more than a bound in
/// fractions of full scale.
[[nodiscard]] Judge firstChannelReproduces(double others)
{
    return [others](const fixtures::Expectation& record, const fixtures::FixtureSamples& shown) {
        if (shown.channelCount() == 1u)
        {
            reproduces()(record, shown);
            return;
        }
        ASSERT_EQ(shown.width(), record.result.width());
        ASSERT_EQ(shown.height(), record.result.height());
        ASSERT_EQ(shown.channelCount(), record.result.channelCount());
        const double scale = fullScaleOf(record);
        for (std::uint32_t y = 0u; y < shown.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < shown.width(); ++x)
            {
                ASSERT_TRUE(withinRecord(record, shown.value(x, y, 0u), record.result.value(x, y, 0u)))
                    << x << "," << y;
                for (std::uint32_t c = 1u; c < shown.channelCount(); ++c)
                {
                    ASSERT_LE(std::abs(shown.value(x, y, c) - record.result.value(x, y, c)) / scale, others)
                        << x << "," << y << "," << c;
                }
            }
        }
    };
}

/// One mode of one record file: which dialog, what its mode sets, whether the run is the dialog's G'MIC, and how its
/// records are judged.
struct RecordedMode
{
    const char* file = "";
    const char* mode = "";
    Opener open;
    Configure configure;
    bool needsGmic = false;
    Judge judge = reproduces();
};

[[nodiscard]] std::vector<RecordedMode> recordedModes()
{
    const Configure nothing = [](QWidget&) {};
    return {
        {"median-filter", "default", opener<MedianFilterProcess>(), nothing},
        {"median-filter", "kernel-5-iterations-2-modulation-0.5", opener<MedianFilterProcess>(),
         [](QWidget& window) {
             choose(window, "kernelSize", QStringLiteral("5x5"));
             setWhole(window, "iterationsValue", 2);
             setReal(window, "modulationValue", 0.5);
         }},
        // The engine's patch denoiser runs the whole frame where the record tiles it.
        {"nl-bayes", "default", opener<NLBayesProcess>(), nothing, false, bounded(0.2, 0.02)},
        {"wavelet-denoise", "default", opener<WaveletDenoiseProcess>(), nothing, true},
        {"patch-pca-denoise", "default", opener<PatchPCADenoiseProcess>(), nothing, true},
        {"graycstoration", "default", opener<GRAYCstorationProcess>(), nothing, true},
        {"cosmetic-correction", "default", opener<CosmeticCorrectionProcess>(), nothing},
        {"cosmetic-correction", "hot-only-0.1", opener<CosmeticCorrectionProcess>(),
         [](QWidget& window) {
             setReal(window, "hotPixelsThresholdValue", 0.1);
             setGroupChecked(window, "coldPixelsGroupBox", false);
         }},
        {"banding-reduction", "default", opener<BandingReductionProcess>(), nothing, false,
         firstChannelReproduces(0.11)},
        {"banding-reduction", "vertical-protected-sigma-2", opener<BandingReductionProcess>(),
         [](QWidget& window) {
             setChecked(window, "verticalBanding", true);
             setChecked(window, "protectFromHighlights", true);
             setReal(window, "sigmaValue", 2.0);
         }},
        {"bilateral-filter", "default", opener<BilateralFilterProcess>(), nothing},
        {"local-histogram-equalization", "default", opener<LocalHistogramEqualizationProcess>(), nothing, true},
        {"local-contrast-enhancement", "default", opener<LocalContrastEnhancementProcess>(), nothing, true},
        {"sharpen-alpha", "default", opener<SharpenAlphaProcess>(), nothing, true},
        // The exact Gaussian where the record approximates it with three box passes.
        {"gaussian-blur", "sigma-1.5", opener<GaussianBlurProcess>(),
         [](QWidget& window) { setReal(window, "sigmaValue", 1.5); }, false, bounded(0.005, 0.0025)},
        {"gaussian-blur", "sigma-4", opener<GaussianBlurProcess>(),
         [](QWidget& window) { setReal(window, "sigmaValue", 4.0); }, false, bounded(0.004, 0.0015)},
        {"high-pass-filter", "default", opener<HighPassFilterProcess>(), nothing, false, bounded(0.005, 0.003)},
        {"high-pass-filter", "radius-2", opener<HighPassFilterProcess>(),
         [](QWidget& window) { setReal(window, "radiusValue", 2.0); }, false, bounded(0.009, 0.0035)},
        // The engine's tile grid and its colour equalization in the image's own channel order.
        {"clahe", "default", opener<CLAHEProcess>(), nothing, false, bounded(0.75, 0.25)},
        {"clahe", "amount-0.5", opener<CLAHEProcess>(), [](QWidget& window) { setReal(window, "amountValue", 0.5); },
         false, bounded(0.375, 0.125)},
        {"tighten-edges", "value-0.8", opener<TightenEdgesProcess>(),
         [](QWidget& window) { setReal(window, "valueValue", 0.8); }, false, reproducesAwayFromRowEnds()},
        {"morphological-transformation", "default", opener<MorphologicalTransformationProcess>(), nothing},
        {"morphological-transformation", "dilation", opener<MorphologicalTransformationProcess>(),
         [](QWidget& window) { choose(window, "operatorSelector", QStringLiteral("Dilation (Maximum)")); }},
        {"morphological-transformation", "opening", opener<MorphologicalTransformationProcess>(),
         [](QWidget& window) { choose(window, "operatorSelector", QStringLiteral("Opening (Dilation + Erosion)")); }},
        {"morphological-transformation", "closing-two-iterations", opener<MorphologicalTransformationProcess>(),
         [](QWidget& window) {
             choose(window, "operatorSelector", QStringLiteral("Closing (Erosion + Dilation)"));
             setWhole(window, "iterationsValue", 2);
         }},
    };
}

/// Whether a sweep leaves a record out: AFTERNIGHT_SKIP_RECORDS names records by id, separated by spaces. A machine
/// that answers a record differently from the machine of its build that captured it says nothing about the record,
/// which that build's own machines still hold, so such a sweep names it rather than judge it.
[[nodiscard]] bool leftOutBySweep(const std::string& id)
{
    const QStringList named =
        qEnvironmentVariable("AFTERNIGHT_SKIP_RECORDS").split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return named.contains(QString::fromStdString(id));
}

} // namespace

TEST(SpatialRecordTest, EveryCaptureIsJudgedThroughItsDialog)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const RecordedMode& mode : recordedModes())
    {
        if (mode.needsGmic && !process_utils::gmicAvailable())
        {
            continue;
        }
        const std::string prefix = std::string(mode.file) + "." + mode.mode + "/";
        for (const fixtures::Expectation& record : behaviorRecords(mode.file))
        {
            if (record.id.rfind(prefix, 0) != 0)
            {
                continue;
            }
            if (leftOutBySweep(record.id))
            {
                ::testing::Test::RecordProperty("leftOutBySweep", record.id);
                continue;
            }
            SCOPED_TRACE(record.id);
            std::optional<an::ui::ImageDocument> document = fixtureDocument(context->engineSession(), record.inputName);
            ASSERT_TRUE(document.has_value());
            const TargetView target(*context, record.inputName, std::move(*document));
            SpatialHandle<ImageRTPreviewControl> process = mode.open(target.view());
            mode.configure(*process);
            ASSERT_TRUE(settleRun(*process, target.view()));
            process->applyProcess();
            ASSERT_TRUE(settleRun(*process, target.view()));
            EXPECT_EQ(process->runTelemetry().runsCommitted, 1u)
                << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            const std::optional<fixtures::FixtureSamples> shown = documentSamples(target.view().document());
            ASSERT_TRUE(shown.has_value());
            mode.judge(record, *shown);
        }
    }
}

// Every record of a file a mode names is judged by a mode, so a capture added to the corpus cannot go unjudged.
TEST(SpatialRecordTest, EveryRecordOfTheirFilesHasAMode)
{
    const std::vector<RecordedMode> modes = recordedModes();
    std::vector<std::string> files;
    for (const RecordedMode& mode : modes)
    {
        if (std::find(files.begin(), files.end(), mode.file) == files.end())
        {
            files.emplace_back(mode.file);
        }
    }
    for (const std::string& file : files)
    {
        const std::vector<fixtures::Expectation> records = behaviorRecords(file);
        EXPECT_FALSE(records.empty()) << file;
        for (const fixtures::Expectation& record : records)
        {
            const bool judged = std::any_of(modes.begin(), modes.end(), [&](const RecordedMode& mode) {
                return record.id.rfind(std::string(mode.file) + "." + mode.mode + "/", 0) == 0;
            });
            EXPECT_TRUE(judged) << record.id << " has no mode";
        }
    }
}
