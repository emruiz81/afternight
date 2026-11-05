// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Calibration: the names and representations its output options give the files it writes, the calibration of
/// a light by its masters and pedestal, the masters held to the lights, the cosmetic correction mapped on the dark, a
/// stopped light, and the window that runs it all on the thread pool and persists its options.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "processes/CalibrationBatch.h"
#include "processes/ImageCalibrationProcess.h"
#include "processes/WorkflowToolFiles.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stop_token>
#include <string>
#include <vector>

namespace an::tests::processes::workflow
{
namespace
{

using an::ui::processes::CalibrationBatch;
using an::ui::processes::CalibrationBatchSettings;
using an::ui::processes::CalibrationOutputFormat;
using an::ui::processes::CalibrationOutputOptions;

constexpr std::uint32_t kSide = 64u;

/// The lights of a run, as the window's list probes them.
[[nodiscard]] std::vector<ImageListProcessHelpers::ImageListEntry> probed(const an::consumer::EngineSession& session,
                                                                          const QStringList& paths)
{
    std::vector<ImageListProcessHelpers::ImageListEntry> entries;
    for (const QString& path : paths)
    {
        an::Result<an::ui::processes::WorkflowFile> file = an::ui::processes::probeWorkflowFile(session, path, {});
        EXPECT_TRUE(file.hasValue()) << file.status().message();
        if (file.hasValue())
        {
            entries.push_back(file->entry);
        }
    }
    return entries;
}

/// A window of the case, deleted once its work has landed.
struct CalibrationWindow
{
    CalibrationWindow() : window(new ImageCalibrationProcess())
    {
        window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~CalibrationWindow()
    {
        (void)pumpUntil([this]() { return window->isIdle(); });
        delete window;
        QCoreApplication::processEvents();
    }
    CalibrationWindow(const CalibrationWindow&) = delete;
    CalibrationWindow& operator=(const CalibrationWindow&) = delete;

    ImageCalibrationProcess* window;
};

class CalibrationTest : public WorkflowCase
{
protected:
    /// A light, a bias, a dark and a flat of one value each, as sixteen-bit FITS files.
    void writeMasters()
    {
        ASSERT_TRUE(writeFlatFrame(session(), light(), kSide, kSide, 1u, 0.5, an::SampleType::UInt16, 120.0));
        ASSERT_TRUE(writeFlatFrame(session(), bias(), kSide, kSide, 1u, 0.1, an::SampleType::UInt16));
        ASSERT_TRUE(writeFlatFrame(session(), dark(), kSide, kSide, 1u, 0.05, an::SampleType::UInt16));
        ASSERT_TRUE(writeFlatFrame(session(), flat(), kSide, kSide, 1u, 0.8, an::SampleType::UInt16));
    }

    [[nodiscard]] QString light() const
    {
        return temporaryPath(QStringLiteral("light.fit"));
    }
    [[nodiscard]] QString bias() const
    {
        return temporaryPath(QStringLiteral("bias.fits"));
    }
    [[nodiscard]] QString dark() const
    {
        return temporaryPath(QStringLiteral("dark.fits"));
    }
    [[nodiscard]] QString flat() const
    {
        return temporaryPath(QStringLiteral("flat.fits"));
    }
    [[nodiscard]] QString output() const
    {
        return temporaryPath(QStringLiteral("calibrated"));
    }

    [[nodiscard]] CalibrationBatchSettings allMasters() const
    {
        CalibrationBatchSettings settings;
        settings.biasPath = bias();
        settings.darkPath = dark();
        settings.flatPath = flat();
        settings.outputDirectory = output();
        return settings;
    }
};

TEST(CalibrationOutputTest, NamesFollowTheFormatAndTheBaseName)
{
    CalibrationOutputOptions options;
    EXPECT_EQ(an::ui::processes::calibrationOutputPath(QStringLiteral("/in/M31_001.fit"), QStringLiteral("/out"),
                                                       options, 3u),
              QStringLiteral("/out/M31_001_cal.fit"));
    EXPECT_EQ(an::ui::processes::calibrationOutputPath(QStringLiteral("/in/M31_001.CR2"), QStringLiteral("/out"),
                                                       options, 3u),
              QStringLiteral("/out/M31_001_cal.fits"));
    options.format = CalibrationOutputFormat::Tiff;
    EXPECT_EQ(an::ui::processes::calibrationOutputPath(QStringLiteral("/in/M31_001.fits"), QStringLiteral("/out"),
                                                       options, 3u),
              QStringLiteral("/out/M31_001_cal.tiff"));
    options.format = CalibrationOutputFormat::Original;
    EXPECT_EQ(an::ui::processes::calibrationOutputPath(QStringLiteral("/in/M31_001.CR2"), QStringLiteral("/out"),
                                                       options, 3u),
              QStringLiteral("/out/M31_001_cal.CR2"));
    EXPECT_EQ(an::ui::processes::calibrationOutputPath(QStringLiteral("/in/raw"), QStringLiteral("/out"), options, 3u),
              QStringLiteral("/out/raw_cal.fits"));
    options.preserveOriginalName = false;
    options.filenameSuffix = QStringLiteral("_c");
    EXPECT_EQ(
        an::ui::processes::calibrationOutputPath(QStringLiteral("/in/raw.fit"), QStringLiteral("/out"), options, 7u),
        QStringLiteral("/out/image_0007_c.fit"));
}

TEST(CalibrationOutputTest, TheRepresentationFollowsThePrecisionOrTheLight)
{
    CalibrationOutputOptions options;
    EXPECT_EQ(an::ui::processes::calibrationOutputRepresentation(options, an::SampleType::UInt16),
              an::SampleType::Float32);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::UInt16),
              an::SampleType::Float32);
    options.use32BitFloat = false;
    EXPECT_EQ(an::ui::processes::calibrationOutputRepresentation(options, an::SampleType::UInt16),
              an::SampleType::Float64);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::UInt16),
              an::SampleType::Float64);
    options.format = CalibrationOutputFormat::Original;
    EXPECT_EQ(an::ui::processes::calibrationOutputRepresentation(options, an::SampleType::UInt16),
              an::SampleType::UInt16);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::UInt16),
              an::SampleType::Float32);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::UInt32),
              an::SampleType::Float64);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::Int32),
              an::SampleType::Float64);
    EXPECT_EQ(an::ui::processes::calibrationWorkingRepresentation(options, an::SampleType::Int16),
              an::SampleType::Float32);
    EXPECT_FALSE(an::ui::processes::calibrationOutputPrecisionApplies(CalibrationOutputFormat::Original));
    EXPECT_TRUE(an::ui::processes::calibrationOutputPrecisionApplies(CalibrationOutputFormat::Tiff));
}

TEST(CalibrationOutputTest, AFormatTextReadsCaseAsideAndAnUnknownOneAsFits)
{
    EXPECT_EQ(an::ui::processes::calibrationOutputFormatFromText(QStringLiteral(" ORIGINAL ")),
              CalibrationOutputFormat::Original);
    EXPECT_EQ(an::ui::processes::calibrationOutputFormatFromText(QStringLiteral("tiff")),
              CalibrationOutputFormat::Tiff);
    EXPECT_EQ(an::ui::processes::calibrationOutputFormatFromText(QStringLiteral("xisf")),
              CalibrationOutputFormat::Fits);
    for (const CalibrationOutputFormat format :
         {CalibrationOutputFormat::Fits, CalibrationOutputFormat::Tiff, CalibrationOutputFormat::Original})
    {
        EXPECT_EQ(
            an::ui::processes::calibrationOutputFormatFromText(an::ui::processes::calibrationOutputFormatText(format)),
            format);
    }
}

TEST(CalibrationOutputTest, ARunCalibratesWhenItNamesAMasterOrAPedestal)
{
    CalibrationBatchSettings settings;
    EXPECT_FALSE(an::ui::processes::calibrationBatchCalibrates(settings));
    settings.pedestalCodes = 100.0;
    EXPECT_TRUE(an::ui::processes::calibrationBatchCalibrates(settings));
    settings.pedestalCodes = 0.0;
    settings.flatPath = QStringLiteral("flat.fits");
    EXPECT_TRUE(an::ui::processes::calibrationBatchCalibrates(settings));
}

TEST_F(CalibrationTest, ALightIsPedestalledAndHasItsMastersSubtractedAndItsFlatDivided)
{
    writeMasters();
    CalibrationBatchSettings settings = allMasters();
    // 655.35 codes is a hundredth of full scale.
    settings.pedestalCodes = 655.35;
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_EQ(outcome->output, QDir(output()).filePath(QStringLiteral("light_cal.fit")));
    ASSERT_TRUE(outcome->darkScale.has_value());
    EXPECT_DOUBLE_EQ(*outcome->darkScale, 1.0);

    const std::optional<ReadBack> written = readBack(session(), outcome->output);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->type, an::SampleType::Float32);
    // The flat normalized by its central mean is one everywhere: the light keeps what the masters leave of it.
    EXPECT_NEAR(written->at(10u, 10u), 0.5 + 0.01 - 0.1 - 0.05, 2e-4);
    EXPECT_NEAR(written->at(40u, 50u), 0.5 + 0.01 - 0.1 - 0.05, 2e-4);
}

TEST_F(CalibrationTest, AFlatNotNormalizedDividesTheLightByItsValue)
{
    writeMasters();
    CalibrationBatchSettings settings = allMasters();
    settings.normalizeFlats = false;
    settings.output.use32BitFloat = false;
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    const std::optional<ReadBack> written = readBack(session(), outcome->output);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->type, an::SampleType::Float64);
    EXPECT_NEAR(written->at(5u, 5u), (0.5 - 0.1 - 0.05) / 0.8, 2e-4);
}

TEST_F(CalibrationTest, TheLightsOwnFormatKeepsItsContainerAndRepresentation)
{
    writeMasters();
    CalibrationBatchSettings settings = allMasters();
    settings.flatPath.clear();
    settings.output.format = CalibrationOutputFormat::Original;
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_TRUE(outcome->output.endsWith(QStringLiteral("light_cal.fit")));
    const std::optional<ReadBack> written = readBack(session(), outcome->output);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->type, an::SampleType::UInt16);
    EXPECT_NEAR(written->at(3u, 4u), 0.5 - 0.1 - 0.05, 2.0 / 65535.0);
    EXPECT_TRUE(written->card("EXPTIME").has_value());
}

TEST_F(CalibrationTest, AMasterOfAnotherSizeIsRefusedByName)
{
    writeMasters();
    ASSERT_TRUE(writeFlatFrame(session(), dark(), kSide / 2u, kSide / 2u, 1u, 0.05, an::SampleType::UInt16));
    an::Result<CalibrationBatch> batch =
        CalibrationBatch::open(session(), allMasters(), probed(session(), {light()}), {});
    ASSERT_FALSE(batch.hasValue());
    EXPECT_EQ(batch.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(batch.status().message()).find("Calibration dark file size is different"), std::string::npos)
        << batch.status().message();
}

TEST_F(CalibrationTest, ARunThatCalibratesNothingOrHasNoLightIsRefused)
{
    writeMasters();
    CalibrationBatchSettings none;
    none.outputDirectory = output();
    EXPECT_EQ(CalibrationBatch::open(session(), none, probed(session(), {light()}), {}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(CalibrationBatch::open(session(), allMasters(), {}, {}).status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(CalibrationTest, TheCosmeticCorrectionReplacesThePixelsTheDarkMarks)
{
    // A dark with a little deterministic noise and two hot pixels, and a light whose same pixels are hot too.
    const auto noisy = [](std::uint32_t x, std::uint32_t y) {
        return 0.002 * static_cast<double>((x * 7u + y * 13u) % 5u);
    };
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 1u, an::SampleType::Float32,
                           [&noisy](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               return (x == 20u && y == 20u) || (x == 41u && y == 9u) ? 0.9 : 0.05 + noisy(x, y);
                           }));
    ASSERT_TRUE(writeFrame(session(), light(), kSide, kSide, 1u, an::SampleType::Float32,
                           [&noisy](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               return (x == 20u && y == 20u) || (x == 41u && y == 9u) ? 0.99 : 0.5 + noisy(x, y);
                           }));
    const an::Result<an::ui::processes::CosmeticEstimate> estimate =
        an::ui::processes::estimateCosmeticDefects(session(), dark(), 3.0, 3.0, {});
    ASSERT_TRUE(estimate.hasValue()) << estimate.status().message();
    EXPECT_EQ(estimate->hot, 2u);
    EXPECT_EQ(estimate->cold, 0u);

    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.cosmeticCorrection = true;
    settings.outputDirectory = output();
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 2u);
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_EQ(outcome->corrected, 2u);
    const std::optional<ReadBack> written = readBack(session(), outcome->output);
    ASSERT_TRUE(written.has_value());
    // The hot pixel left the dark's subtraction far above its neighbours; corrected, it is one of them.
    EXPECT_LT(written->at(20u, 20u), 0.5);
    EXPECT_NEAR(written->at(20u, 20u), written->at(21u, 20u), 0.02);
}

TEST_F(CalibrationTest, AnUncheckedSigmaLooksForNoPixelsOfItsPolarity)
{
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 1u, an::SampleType::Float32,
                           [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               return x == 7u && y == 7u ? 0.9 : 0.05 + 0.001 * static_cast<double>((x + y) % 3u);
                           }));
    const an::Result<an::ui::processes::CosmeticEstimate> estimate =
        an::ui::processes::estimateCosmeticDefects(session(), dark(), 3.0, std::nullopt, {});
    ASSERT_TRUE(estimate.hasValue()) << estimate.status().message();
    EXPECT_EQ(estimate->hot, 0u);
}

TEST_F(CalibrationTest, AStoppedLightLeavesNoFile)
{
    writeMasters();
    an::Result<CalibrationBatch> batch =
        CalibrationBatch::open(session(), allMasters(), probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome =
        batch->calibrate(light(), 0u, stop.get_token());
    ASSERT_FALSE(outcome.hasValue());
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(QDir(output()).entryList(QDir::Files).isEmpty());
}

TEST_F(CalibrationTest, LightsOfMoreThanOneColourDepthAreRefusedByName)
{
    writeMasters();
    const QString colour = temporaryPath(QStringLiteral("colour.fit"));
    ASSERT_TRUE(writeFlatFrame(session(), colour, kSide, kSide, 3u, 0.5, an::SampleType::UInt16));
    CalibrationBatchSettings settings;
    settings.pedestalCodes = 100.0;
    settings.outputDirectory = output();
    const an::Result<CalibrationBatch> batch =
        CalibrationBatch::open(session(), settings, probed(session(), {light(), colour}), {});
    ASSERT_FALSE(batch.hasValue());
    EXPECT_EQ(batch.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(batch.status().message()).find("different color depth"), std::string::npos)
        << batch.status().message();
}

TEST_F(CalibrationTest, AMasterOfAnotherHeightOrDepthIsRefusedByName)
{
    writeMasters();
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.outputDirectory = output();
    ASSERT_TRUE(writeFlatFrame(session(), dark(), kSide, kSide / 2u, 1u, 0.05, an::SampleType::UInt16));
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_FALSE(batch.hasValue());
    EXPECT_NE(std::string(batch.status().message()).find("Calibration dark file size is different"), std::string::npos)
        << batch.status().message();

    // A master of more channels than the light is not the light's depth.
    ASSERT_TRUE(writeFlatFrame(session(), dark(), kSide, kSide, 3u, 0.05, an::SampleType::UInt16));
    batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_FALSE(batch.hasValue());
    EXPECT_NE(std::string(batch.status().message()).find("Calibration dark file depth is different"), std::string::npos)
        << batch.status().message();

    // And one of fewer channels than a colour light.
    const QString colour = temporaryPath(QStringLiteral("colour.fit"));
    ASSERT_TRUE(writeFlatFrame(session(), colour, kSide, kSide, 3u, 0.5, an::SampleType::UInt16));
    ASSERT_TRUE(writeFlatFrame(session(), dark(), kSide, kSide, 1u, 0.05, an::SampleType::UInt16));
    batch = CalibrationBatch::open(session(), settings, probed(session(), {colour}), {});
    ASSERT_FALSE(batch.hasValue());
    EXPECT_NE(std::string(batch.status().message()).find("Calibration dark file depth is different"), std::string::npos)
        << batch.status().message();
}

TEST_F(CalibrationTest, EachPolarityIsMappedAtItsOwnThreshold)
{
    // A dark with a little deterministic noise, two hot pixels and one cold one.
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 1u, an::SampleType::Float32,
                           [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               if ((x == 20u && y == 20u) || (x == 41u && y == 9u))
                               {
                                   return 0.9;
                               }
                               if (x == 8u && y == 50u)
                               {
                                   return 0.0;
                               }
                               return 0.3 + 0.002 * static_cast<double>((x * 7u + y * 13u) % 5u);
                           }));
    ASSERT_TRUE(writeFlatFrame(session(), light(), kSide, kSide, 1u, 0.5, an::SampleType::Float32));
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.cosmeticCorrection = true;
    settings.outputDirectory = output();

    // A threshold no sample reaches finds nothing of its polarity.
    settings.coldSigma = 1000.0;
    settings.hotSigma = 3.0;
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 2u);
    EXPECT_EQ(batch->coldPixels(), 0u);

    settings.coldSigma = 3.0;
    settings.hotSigma = 1000.0;
    batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 0u);
    EXPECT_EQ(batch->coldPixels(), 1u);
}

TEST_F(CalibrationTest, OnePolarityAloneBuildsItsMapAndCorrectsTheLight)
{
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 1u, an::SampleType::Float32,
                           [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               if ((x == 20u && y == 20u) || (x == 41u && y == 9u))
                               {
                                   return 0.9;
                               }
                               if (x == 8u && y == 50u)
                               {
                                   return 0.0;
                               }
                               return 0.3 + 0.002 * static_cast<double>((x * 7u + y * 13u) % 5u);
                           }));
    ASSERT_TRUE(writeFlatFrame(session(), light(), kSide, kSide, 1u, 0.5, an::SampleType::Float32));
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.cosmeticCorrection = true;
    settings.outputDirectory = output();

    // Only hot pixels are asked for: the cold map is not built, the hot one is, and the light is corrected by it.
    settings.coldSigma.reset();
    settings.hotSigma = 3.0;
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 2u);
    EXPECT_EQ(batch->coldPixels(), 0u);
    const an::Result<an::ui::processes::CalibrationFrameOutcome> hotOnly = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(hotOnly.hasValue()) << hotOnly.status().message();
    EXPECT_EQ(hotOnly->corrected, 2u);

    // And the cold polarity alone.
    settings.hotSigma.reset();
    settings.coldSigma = 3.0;
    batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 0u);
    EXPECT_EQ(batch->coldPixels(), 1u);
    const an::Result<an::ui::processes::CalibrationFrameOutcome> coldOnly = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(coldOnly.hasValue()) << coldOnly.status().message();
    EXPECT_EQ(coldOnly->corrected, 1u);
}

TEST_F(CalibrationTest, ALightStoppedAfterItIsCalibratedIsNotWritten)
{
    ASSERT_TRUE(writeFlatFrame(session(), light(), kSide, kSide, 3u, 0.5, an::SampleType::Float32));
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 3u, an::SampleType::Float32,
                           [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               return 0.05 + 0.001 * static_cast<double>((x * 3u + y) % 7u);
                           }));
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.darkOptimization = true;
    settings.outputDirectory = output();
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();

    // A colour light's dark scale is logged once it is calibrated and before its file is written: the stop lands there.
    std::stop_source stop;
    LogCapture log;
    log.onRecord("could not be optimized", [&stop]() { stop.request_stop(); });
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome =
        batch->calibrate(light(), 0u, stop.get_token());
    ASSERT_GT(log.count("could not be optimized"), 0u);
    ASSERT_FALSE(outcome.hasValue());
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(QDir(output()).entryList(QDir::Files).isEmpty());
}

TEST_F(CalibrationTest, AnOptimizedDarkScaleFallsBackToTheFixedOneForAColourLight)
{
    ASSERT_TRUE(writeFlatFrame(session(), light(), kSide, kSide, 3u, 0.5, an::SampleType::Float32));
    ASSERT_TRUE(writeFrame(session(), dark(), kSide, kSide, 3u, an::SampleType::Float32,
                           [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                               return 0.05 + 0.001 * static_cast<double>((x * 3u + y) % 7u);
                           }));
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.darkOptimization = true;
    settings.darkMultiplicationFactor = 0.5;
    settings.outputDirectory = output();
    an::Result<CalibrationBatch> batch = CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome = batch->calibrate(light(), 0u, {});
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_TRUE(outcome->darkScaleFixedInstead);
    ASSERT_TRUE(outcome->darkScale.has_value());
    EXPECT_DOUBLE_EQ(*outcome->darkScale, 0.5);
}

TEST_F(CalibrationTest, TheWindowCalibratesEveryLightAndMarksItsStatus)
{
    writeMasters();
    const QString second = temporaryPath(QStringLiteral("light2.fit"));
    ASSERT_TRUE(writeFlatFrame(session(), second, kSide, kSide, 1u, 0.6, an::SampleType::UInt16, 120.0));
    CalibrationWindow window;
    window.window->addItemsNonBlocking({light(), second});
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(window.window->fileList().files().size(), 2u);
    EXPECT_EQ(window.window->statusOf(light()), QStringLiteral("Pending"));

    auto& ui = window.window->ui();
    ui.biasGroupBox->setChecked(true);
    ui.biasFilePath->setText(bias());
    ui.darkGroupBox->setChecked(true);
    ui.darkFilePath->setText(dark());
    ui.outputDirectory->setText(output());
    EXPECT_TRUE(ui.calibrateImagesButton->isEnabled());
    window.window->calibrate();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_EQ(window.window->statusOf(light()), QStringLiteral("Calibrated"));
    EXPECT_EQ(window.window->statusOf(second), QStringLiteral("Calibrated"));
    const std::optional<ReadBack> written =
        readBack(session(), QDir(output()).filePath(QStringLiteral("light2_cal.fit")));
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->at(1u, 1u), 0.6 - 0.1 - 0.05, 2e-4);
}

TEST_F(CalibrationTest, TheWindowAsksForAnOutputDirectoryBeforeItCalibrates)
{
    writeMasters();
    CalibrationWindow window;
    window.window->addItemsNonBlocking({light()});
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->ui().pedestalValue->setValue(100.0);
    const qsizetype before = context().messages.size();
    window.window->calibrate();
    EXPECT_EQ(context().messages.size(), before + 1);
    EXPECT_TRUE(window.window->isIdle());
}

TEST_F(CalibrationTest, TheWindowSaysWhyItCannotCalibrateWhileFilesAreAdded)
{
    writeMasters();
    const QString second = temporaryPath(QStringLiteral("light2.fit"));
    ASSERT_TRUE(writeFlatFrame(session(), second, kSide, kSide, 1u, 0.6, an::SampleType::UInt16, 120.0));
    CalibrationWindow window;
    window.window->addItemsNonBlocking({light()});
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->ui().pedestalValue->setValue(100.0);
    window.window->ui().outputDirectory->setText(output());

    // The second file's import has not landed: the window is busy and says so rather than nothing.
    window.window->addItemsNonBlocking({second});
    ASSERT_FALSE(window.window->isIdle());
    const qsizetype before = context().messages.size();
    window.window->calibrate();
    EXPECT_EQ(context().messages.size(), before + 1);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_FALSE(QDir(output()).exists());
}

TEST_F(CalibrationTest, ARunStartsEveryRowPendingAgain)
{
    writeMasters();
    CalibrationWindow window;
    window.window->addItemsNonBlocking({light()});
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->ui().pedestalValue->setValue(100.0);
    window.window->ui().outputDirectory->setText(output());
    window.window->calibrate();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(window.window->statusOf(light()), QStringLiteral("Calibrated"));

    // A second run states nothing of the first: a row is pending until this run reaches it.
    window.window->calibrate();
    EXPECT_FALSE(window.window->isIdle());
    EXPECT_EQ(window.window->statusOf(light()), QStringLiteral("Pending"));
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_EQ(window.window->statusOf(light()), QStringLiteral("Calibrated"));
}

TEST_F(CalibrationTest, ACosmeticCorrectionWithNeitherSigmaSaysItCorrectsNothing)
{
    writeMasters();
    CalibrationBatchSettings settings;
    settings.darkPath = dark();
    settings.cosmeticCorrection = true;
    settings.coldSigma.reset();
    settings.hotSigma.reset();
    settings.outputDirectory = output();
    LogCapture log;
    const an::Result<CalibrationBatch> batch =
        CalibrationBatch::open(session(), settings, probed(session(), {light()}), {});
    ASSERT_TRUE(batch.hasValue()) << batch.status().message();
    EXPECT_EQ(batch->hotPixels(), 0u);
    EXPECT_EQ(batch->coldPixels(), 0u);
    EXPECT_EQ(log.count("neither the cold nor the hot sigma"), 1u);
}

TEST_F(CalibrationTest, TheWindowSaysACosmeticCorrectionWithNeitherSigmaCorrectedNothing)
{
    writeMasters();
    // A light gone from the disk once it is listed fails, so the second run completes with warnings of its own.
    const QString gone = temporaryPath(QStringLiteral("gone.fit"));
    for (const bool withFailure : {false, true})
    {
        SCOPED_TRACE(withFailure ? "a light fails" : "every light calibrates");
        CalibrationWindow window;
        QStringList lights{light()};
        if (withFailure)
        {
            ASSERT_TRUE(writeFlatFrame(session(), gone, kSide, kSide, 1u, 0.5, an::SampleType::UInt16, 120.0));
            lights << gone;
        }
        window.window->addItemsNonBlocking(lights);
        ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
        ASSERT_EQ(window.window->fileList().files().size(), static_cast<std::size_t>(lights.size()));
        if (withFailure)
        {
            ASSERT_TRUE(QFile::remove(gone));
        }
        auto& ui = window.window->ui();
        ui.darkGroupBox->setChecked(true);
        ui.darkFilePath->setText(dark());
        ui.cosmeticCorrectionGroupBox->setChecked(true);
        ui.coldSigmaCheck->setChecked(false);
        ui.hotSigmaCheck->setChecked(false);
        ui.outputDirectory->setText(output());
        LogCapture log;
        window.window->calibrate();
        ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
        // The run's end is shown in the window's banner and written to the log.
        const std::vector<std::string> records = log.messages();
        const auto said = std::find_if(records.begin(), records.end(), [](const std::string& record) {
            return record.find("no pixel was corrected") != std::string::npos;
        });
        ASSERT_NE(said, records.end()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        if (withFailure)
        {
            EXPECT_NE(said->find("First error"), std::string::npos) << *said;
        }
    }
}

// The dark scaling factor scales every light's dark while dark optimization is off, and the dark of each light the
// optimization cannot measure - a colour light, or one smaller than its central square - while it is on, so it is
// offered either way once a dark is chosen.
TEST_F(CalibrationTest, TheDarkScalingIsOfferedWhereverItScalesADark)
{
    writeMasters();
    CalibrationWindow window;
    window.window->addItemsNonBlocking({light()});
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    auto& ui = window.window->ui();
    EXPECT_FALSE(ui.darkMultiplicationFactorValue->isEnabled()) << "no dark is chosen";
    ui.darkGroupBox->setChecked(true);
    ui.darkFilePath->setText(dark());
    EXPECT_TRUE(ui.darkMultiplicationFactorValue->isEnabled());
    ui.darkOptimizationCheck->setChecked(true);
    EXPECT_TRUE(ui.darkMultiplicationFactorValue->isEnabled()) << "a light the optimization cannot measure reads it";
}

TEST_F(CalibrationTest, TheWindowPersistsEveryOptionAndItsFrames)
{
    writeMasters();
    std::string stored;
    {
        CalibrationWindow window;
        window.window->addItemsNonBlocking({light()});
        ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
        auto& ui = window.window->ui();
        ui.biasGroupBox->setChecked(true);
        ui.biasFilePath->setText(bias());
        ui.darkGroupBox->setChecked(true);
        ui.darkFilePath->setText(dark());
        ui.darkOptimizationCheck->setChecked(true);
        ui.darkMultiplicationFactorValue->setValue(1.5);
        ui.cosmeticCorrectionGroupBox->setChecked(true);
        ui.coldSigmaCheck->setChecked(false);
        ui.hotSigmaValue->setValue(4.5);
        ui.flatGroupBox->setChecked(true);
        ui.flatFilePath->setText(flat());
        ui.normalizeFlatsCheck->setChecked(false);
        ui.preserveOriginalNameCheck->setChecked(false);
        ui.filenameSuffixEdit->setText(QStringLiteral("_x"));
        ui.outputFormatSelector->setCurrentIndex(ui.outputFormatSelector->findData(QStringLiteral("tiff")));
        ui.outputPrecisionSelector->setCurrentIndex(1);
        ui.pedestalValue->setValue(250.0);
        ui.outputDirectory->setText(output());
        stored = window.window->serialize_json_settings();
    }
    const nlohmann::json document = nlohmann::json::parse(stored);
    EXPECT_EQ(document.value("version", 0), 3);
    const auto& s = document["settings"];
    EXPECT_EQ(s.value("dark_multiplication_factor", 0.0), 1.5);
    EXPECT_EQ(s.value("output_format", std::string()), "tiff");
    EXPECT_FALSE(s.value("use_32bit_float", true));
    EXPECT_FALSE(s.value("cold_sigma_enabled", true));

    CalibrationWindow restored;
    restored.window->deserialize_json_settings(stored);
    ASSERT_TRUE(pumpUntil([&restored]() { return restored.window->isIdle(); }));
    EXPECT_EQ(restored.window->fileList().paths(), QStringList{light()});
    const CalibrationBatchSettings settings = restored.window->settings();
    EXPECT_EQ(settings.biasPath, bias());
    EXPECT_EQ(settings.darkPath, dark());
    EXPECT_EQ(settings.flatPath, flat());
    EXPECT_TRUE(settings.darkOptimization);
    EXPECT_DOUBLE_EQ(settings.darkMultiplicationFactor, 1.5);
    EXPECT_TRUE(settings.cosmeticCorrection);
    EXPECT_FALSE(settings.coldSigma.has_value());
    ASSERT_TRUE(settings.hotSigma.has_value());
    EXPECT_DOUBLE_EQ(*settings.hotSigma, 4.5);
    EXPECT_FALSE(settings.normalizeFlats);
    EXPECT_FALSE(settings.output.preserveOriginalName);
    EXPECT_EQ(settings.output.filenameSuffix, QStringLiteral("_x"));
    EXPECT_EQ(settings.output.format, CalibrationOutputFormat::Tiff);
    EXPECT_FALSE(settings.output.use32BitFloat);
    EXPECT_DOUBLE_EQ(settings.pedestalCodes, 250.0);
    EXPECT_EQ(settings.outputDirectory, output());
}

TEST_F(CalibrationTest, ClosingTheWindowStopsItsRunAndWaitsForNothing)
{
    writeMasters();
    QStringList lights;
    for (int index = 0; index < 6; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("many_%1.fit").arg(index));
        ASSERT_TRUE(writeFlatFrame(session(), path, 256u, 256u, 1u, 0.5, an::SampleType::UInt16));
        lights << path;
    }
    QPointer<ImageCalibrationProcess> window(new ImageCalibrationProcess());
    window->addItemsNonBlocking(lights);
    ASSERT_TRUE(pumpUntil([&window]() { return window->isIdle(); }));
    window->ui().pedestalValue->setValue(100.0);
    window->ui().outputDirectory->setText(output());
    window->calibrate();
    EXPECT_FALSE(window->isIdle());
    window->close();
    // The window deletes itself on close; what its run would land is dropped.
    ASSERT_TRUE(pumpUntil([&window]() { return window.isNull(); }));
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
}

// At exit the application stops every window still open before it drains the thread pool and deletes them
// (`an::ui::destroyProcessWindowsAtExit`). A batch on the pool ends at its next stop point through the window's stop
// token - the one its run took when it started - so the exit waits for that, never for the batch's last frame. Before
// the window's run took the base's token, the exit asked a stop of the window that reached nothing it ran and then
// waited for the whole batch.
TEST_F(CalibrationTest, TheApplicationExitingWithTheWindowOpenStopsItsRunBeforeItDrainsThePool)
{
    writeMasters();
    QStringList lights;
    for (int index = 0; index < 24; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("exit_%1.fit").arg(index));
        ASSERT_TRUE(writeFlatFrame(session(), path, 512u, 512u, 1u, 0.5, an::SampleType::UInt16));
        lights << path;
    }
    QPointer<ImageCalibrationProcess> window(new ImageCalibrationProcess());
    window->addItemsNonBlocking(lights);
    ASSERT_TRUE(pumpUntil([&window]() { return window->isIdle(); }));
    window->ui().pedestalValue->setValue(100.0);
    window->ui().outputDirectory->setText(output());
    window->calibrate();
    ASSERT_FALSE(window->isIdle());

    an::ui::destroyProcessWindowsAtExit({QPointer<TopLevelProcessBaseControl>(window.data())});
    EXPECT_TRUE(window.isNull());
    QCoreApplication::processEvents();
    // The run stopped at the light after the stop reached it: calibrating every light outlasts the exit by far.
    const int written = QDir(output()).entryList(QDir::Files).size();
    EXPECT_LT(written, lights.size()) << "the exit waited for the whole batch";
}

} // namespace
} // namespace an::tests::processes::workflow
