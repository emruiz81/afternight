// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Stacking: the engine's stack request its settings state through the integration's choice maps, the settings
/// a combination lets go, the exposure a stack states, the rejection maps' names and fractions, a group stacked from
/// files - weighed by each frame's measured noise when asked, an X-Trans mosaic demosaiced under its own filter array,
/// what the engine warned of the stack kept and logged - and the window that groups the files, stacks every group on
/// the thread pool, says what the stacks warned and persists its options.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialog.h"
#include "processes/ImageStackingProcess.h"
#include "processes/StackingBatch.h"
#include "processes/WorkflowToolFiles.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace an::tests::processes::workflow
{
namespace
{

using an::ui::StandaloneStackingFrame;
using an::ui::StandaloneStackingGroup;
using an::ui::processes::StandaloneStackRun;

constexpr std::uint32_t kSide = 48u;

/// A deterministic number in [-1, 1] for a sample of a frame, from a hash of where it lies.
[[nodiscard]] double hashedUniform(std::uint32_t x, std::uint32_t y, std::uint32_t frame)
{
    std::uint64_t hash = (static_cast<std::uint64_t>(x) * 73856093u) ^ (static_cast<std::uint64_t>(y) * 19349663u) ^
                         (static_cast<std::uint64_t>(frame + 1u) * 83492791u);
    hash ^= hash >> 33u;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33u;
    hash *= 0xc4ceb9fe1a85ec53ull;
    hash ^= hash >> 33u;
    return static_cast<double>(hash >> 11u) / static_cast<double>(std::uint64_t{1} << 53u) * 2.0 - 1.0;
}

/// A failure's message and every cause's below it, for a case that looks for a word anywhere in the chain.
[[nodiscard]] std::string messageChain(an::Status status)
{
    std::string chain;
    while (status.failed())
    {
        chain += status.message();
        chain += " | ";
        status = status.cause();
    }
    return chain;
}

/// Holds the process in a numeric locale whose decimal separator is a comma, as QApplication leaves a Linux host
/// configured with one, and restores the locale it found. Not active when the host installs none.
class CommaDecimalLocale
{
public:
    CommaDecimalLocale()
    {
        const char* const current = std::setlocale(LC_NUMERIC, nullptr);
        m_saved = current != nullptr ? current : "C";
        for (const char* candidate : {"es_AR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "de-DE", "es-AR"})
        {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr && std::localeconv()->decimal_point[0] == ',')
            {
                m_active = true;
                return;
            }
        }
        std::setlocale(LC_NUMERIC, m_saved.c_str());
    }

    ~CommaDecimalLocale()
    {
        std::setlocale(LC_NUMERIC, m_saved.c_str());
    }

    CommaDecimalLocale(const CommaDecimalLocale&) = delete;
    CommaDecimalLocale& operator=(const CommaDecimalLocale&) = delete;

    [[nodiscard]] bool active() const noexcept
    {
        return m_active;
    }

private:
    std::string m_saved;
    bool m_active = false;
};

[[nodiscard]] std::optional<an::ImageDescriptor> greyFloat(const an::EngineContext& engine)
{
    return an::tests::viewer::describeImage(engine, an::SampleType::Float32, kSide, kSide,
                                            an::tests::viewer::Channels::Grey);
}

/// The request a settings record states over a description of eight frames.
[[nodiscard]] an::Result<an::StackRequest> requestOf(const an::consumer::EngineSession& session,
                                                     const StackingSettings& settings,
                                                     an::StackWeighting weighting = an::StackWeighting::Uniform,
                                                     bool output64 = false)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    const std::optional<an::ImageDescriptor> descriptor = greyFloat(hold->engine());
    EXPECT_TRUE(descriptor.has_value());
    return an::ui::processes::standaloneStackRequest(settings, *descriptor, 8u, weighting, output64);
}

/// A group of files as the window groups them.
[[nodiscard]] StandaloneStackingGroup groupOf(const an::consumer::EngineSession& session, const QStringList& paths,
                                              const QString& outputName)
{
    StandaloneStackingGroup group;
    group.label = QStringLiteral("group");
    group.outputFileName = outputName;
    for (const QString& path : paths)
    {
        an::Result<an::ui::processes::WorkflowFile> file = an::ui::processes::probeWorkflowFile(session, path, {});
        EXPECT_TRUE(file.hasValue()) << file.status().message();
        if (file.hasValue())
        {
            group.images.push_back(StandaloneStackingFrame{.entry = file->entry,
                                                           .filter = file->figures.filter(),
                                                           .capturedAtUtc = file->figures.capturedAtUtc(),
                                                           .exposureSeconds = file->figures.exposureSeconds()});
        }
    }
    group.frameCount = static_cast<int>(group.images.size());
    return group;
}

struct StackingWindow
{
    StackingWindow() : window(new ImageStackingProcess())
    {
        window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~StackingWindow()
    {
        (void)pumpUntil([this]() { return window->isIdle(); });
        delete window;
        QCoreApplication::processEvents();
    }
    StackingWindow(const StackingWindow&) = delete;
    StackingWindow& operator=(const StackingWindow&) = delete;

    ImageStackingProcess* window;
};

class StackingTest : public WorkflowCase
{
protected:
    /// Three flat frames of 0.2, 0.4 and 0.6, each sixty seconds through a filter.
    [[nodiscard]] QStringList writeThree(const QString& filter = QStringLiteral("L"), const QString& prefix = {})
    {
        QStringList paths;
        for (const double value : {0.2, 0.4, 0.6})
        {
            const QString path =
                temporaryPath(QStringLiteral("%1frame_%2.fits").arg(prefix).arg(static_cast<int>(value * 10.0)));
            EXPECT_TRUE(writeFlatFrame(session(), path, kSide, kSide, 1u, value, an::SampleType::UInt16, 60.0, filter));
            paths << path;
        }
        return paths;
    }

    [[nodiscard]] QString output() const
    {
        return temporaryPath(QStringLiteral("stacked"));
    }

    /// Three Float32 frames of 0.2, 0.4 and 0.6, each with noise of its own: an amplitude of 0.002, 0.02 and 0.04
    /// about its level.
    [[nodiscard]] QStringList writeNoisy()
    {
        QStringList paths;
        const double levels[] = {0.2, 0.4, 0.6};
        const double amplitudes[] = {0.002, 0.02, 0.04};
        for (std::uint32_t index = 0u; index < 3u; ++index)
        {
            const QString path = temporaryPath(QStringLiteral("noisy_%1.fits").arg(index));
            const double level = levels[index];
            const double amplitude = amplitudes[index];
            EXPECT_TRUE(writeFrame(session(), path, kSide, kSide, 1u, an::SampleType::Float32,
                                   [index, level, amplitude](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                       return level + amplitude * hashedUniform(x, y, index);
                                   }));
            paths << path;
        }
        return paths;
    }

    /// Three Float32 frames of nothing but zeros, whose sum no peak normalizes.
    [[nodiscard]] QStringList writeZeros()
    {
        QStringList paths;
        for (int index = 0; index < 3; ++index)
        {
            const QString path = temporaryPath(QStringLiteral("zero_%1.fits").arg(index));
            EXPECT_TRUE(writeFlatFrame(session(), path, kSide, kSide, 1u, 0.0, an::SampleType::Float32));
            paths << path;
        }
        return paths;
    }

    /// A frame of the list with no file behind it, as the weights read one: its path and a grey description.
    [[nodiscard]] StandaloneStackingFrame listedFrame(const QString& path)
    {
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(hold.hasValue());
        const std::optional<an::ImageDescriptor> descriptor = greyFloat(hold->engine());
        EXPECT_TRUE(descriptor.has_value());
        return StandaloneStackingFrame{
            .entry = ImageListProcessHelpers::ImageListEntry{.path = path, .descriptor = *descriptor, .metadata = {}}};
    }
};

/// The mean of every sample a file holds, as fractions of full scale.
[[nodiscard]] double meanOf(const ReadBack& written)
{
    double sum = 0.0;
    for (const double value : written.fractions)
    {
        sum += value;
    }
    return written.fractions.empty() ? 0.0 : sum / static_cast<double>(written.fractions.size());
}

TEST_F(StackingTest, AnAverageTakesTheRejectionTheSettingsName)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
    settings.sigmaLow = 2.5;
    settings.sigmaHigh = 3.5;
    settings.normalizationMode = NormalizationMode::None;
    const an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->combination, an::StackCombination::Average);
    EXPECT_EQ(request->rejection.method, an::StackRejection::WinsorizedSigmaClipping);
    EXPECT_DOUBLE_EQ(request->rejection.sigma.low, 2.5);
    EXPECT_DOUBLE_EQ(request->rejection.sigma.high, 3.5);
    EXPECT_EQ(request->normalization.scope, an::StackNormalizationScope::None);
    EXPECT_EQ(request->output.sampleType, an::SampleType::Float32);
}

TEST_F(StackingTest, EachRejectionReadsItsTwoNumbersAsItsOwnThresholds)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::None;
    settings.rejectionMethod = QStringLiteral("Percentile");
    settings.sigmaLow = 0.25;
    settings.sigmaHigh = 0.15;
    an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->rejection.method, an::StackRejection::PercentileClipping);
    EXPECT_DOUBLE_EQ(request->rejection.medianFractions.low, 0.25);
    EXPECT_DOUBLE_EQ(request->rejection.medianFractions.high, 0.15);

    settings.rejectionMethod = QStringLiteral("GESDT");
    settings.sigmaLow = 0.3;
    settings.sigmaHigh = 0.05;
    request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->rejection.method, an::StackRejection::GeneralizedEsdTest);
    EXPECT_DOUBLE_EQ(request->rejection.gesdt.maximumOutlierFraction, 0.3);
    EXPECT_DOUBLE_EQ(request->rejection.gesdt.significance, 0.05);

    settings.rejectionMethod = QStringLiteral("Linear Fit");
    settings.sigmaLow = 5.0;
    settings.sigmaHigh = 4.0;
    request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->rejection.method, an::StackRejection::LinearFitClipping);
    EXPECT_DOUBLE_EQ(request->rejection.sigma.low, 5.0);
}

TEST_F(StackingTest, AStoredMethodThatNamesARejectionIsAnAverageWithIt)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::None;
    settings.stackMethod = QStringLiteral("Sigma Clipping");
    const an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->combination, an::StackCombination::Average);
    EXPECT_EQ(request->rejection.method, an::StackRejection::SigmaClipping);
}

TEST_F(StackingTest, AnUnknownMethodIsRefusedRatherThanGuessed)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Trimmed Mean");
    const an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_FALSE(request.hasValue());
    EXPECT_EQ(request.status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(StackingTest, TheNormalizationIsTheScopeMethodAndEstimatorTheSettingsName)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Global;
    settings.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    settings.scaleEstimator = ScaleEstimatorMethod::MAD;
    an::Result<an::StackRequest> request = requestOf(session(), settings, an::StackWeighting::Uniform, true);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->normalization.scope, an::StackNormalizationScope::Global);
    ASSERT_TRUE(request->normalization.globalMethod.has_value());
    EXPECT_EQ(*request->normalization.globalMethod, an::NormalizationMethod::MultiplicativeScaling);
    EXPECT_EQ(request->normalization.scaleEstimator, an::ScaleEstimator::MedianAbsoluteDeviation);
    EXPECT_EQ(request->output.sampleType, an::SampleType::Float64);

    settings.normalizationMode = NormalizationMode::Local;
    request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->normalization.scope, an::StackNormalizationScope::Local);
}

TEST_F(StackingTest, RejectionMapsAreMergedOrLowAndHighAsAsked)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::None;
    settings.createRejectionMaps = true;
    settings.mergeRejectionMaps = true;
    an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->output.rejectionMaps, an::RejectionMapForm::Merged);
    settings.mergeRejectionMaps = false;
    request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->output.rejectionMaps, an::RejectionMapForm::LowAndHigh);
}

TEST_F(StackingTest, NoRejectionMapIsAskedForUnlessTheSettingsAskForOne)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::None;
    settings.rejectionMethod = QStringLiteral("Sigma Clipping");
    settings.createRejectionMaps = false;
    settings.mergeRejectionMaps = true;
    an::Result<an::StackRequest> request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->rejection.method, an::StackRejection::SigmaClipping);
    EXPECT_EQ(request->output.rejectionMaps, an::RejectionMapForm::None);
    // And none is made where nothing is rejected.
    settings.createRejectionMaps = true;
    settings.rejectionMethod = QStringLiteral("None");
    request = requestOf(session(), settings);
    ASSERT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->output.rejectionMaps, an::RejectionMapForm::None);
}

TEST(StackingSettingsTest, ACombinationLetsGoOfTheSettingsItCannotUse)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Median");
    settings.weighting = QStringLiteral("Noise");
    settings.createRejectionMaps = true;
    settings.mergeRejectionMaps = true;
    const StackingSettings median = an::ui::processes::sanitizeStandaloneStackingSettings(settings);
    EXPECT_EQ(median.weighting, QStringLiteral("None"));
    EXPECT_EQ(median.rejectionMethod, QStringLiteral("None"));
    EXPECT_FALSE(median.createRejectionMaps);
    EXPECT_FALSE(median.mergeRejectionMaps);

    settings.stackMethod = QStringLiteral("Average");
    settings.cometStackingMode = QStringLiteral("Comet");
    const StackingSettings comet = an::ui::processes::sanitizeStandaloneStackingSettings(settings);
    EXPECT_EQ(comet.weighting, QStringLiteral("None"));
    EXPECT_TRUE(comet.createRejectionMaps);
}

TEST(StackingSettingsTest, TheExposureIsTheReferencesOrTheSumWithTheMissingOnesFilled)
{
    const std::vector<std::optional<double>> exposures{60.0, std::nullopt, 120.0};
    EXPECT_EQ(an::ui::processes::standaloneStackExposure(exposures, 0u, false), 60.0);
    EXPECT_EQ(an::ui::processes::standaloneStackExposure(exposures, 2u, false), 120.0);
    EXPECT_EQ(an::ui::processes::standaloneStackExposure(exposures, 0u, true), 60.0 + 60.0 + 120.0);
    EXPECT_EQ(an::ui::processes::standaloneStackExposure(exposures, 1u, true), 180.0);
    EXPECT_FALSE(an::ui::processes::standaloneStackExposure({std::nullopt, std::nullopt}, 0u, true).has_value());
}

TEST(StackingSettingsTest, ARejectionMapLiesBesideItsStack)
{
    const QString directory = QDir::cleanPath(QDir::tempPath());
    EXPECT_EQ(an::ui::processes::standaloneRejectionMapPath(directory + QStringLiteral("/stacked_L.fit"),
                                                            QStringLiteral("rej_l+h")),
              directory + QStringLiteral("/stacked_L_rej_l+h.fit"));
}

TEST(StackingSettingsTest, TheLocalNormalizationRequestHoldsTheSettingsTheEngineTakes)
{
    LocalNormalizationSettings settings;
    settings.gridSize = 100000;
    settings.gridInterpolation = QStringLiteral("Bilinear");
    settings.backgroundEstimator = QStringLiteral("Percentile");
    settings.backgroundPercentile = 0.4;
    settings.sigmaClipK = 2.5;
    settings.sigmaClipIterations = 4;
    settings.backgroundSmoothingRadius = 2;
    settings.scaleMode = QStringLiteral("Per-Channel");
    settings.localScaleSmoothingRadius = 3;
    const an::LocalNormalizationRequest request = an::ui::processes::standaloneLocalNormalizationRequest(settings);
    EXPECT_EQ(request.gridSize, an::kMaxLocalNormalizationGridSize);
    EXPECT_EQ(request.interpolation, an::LocalGridInterpolation::Bilinear);
    EXPECT_EQ(request.estimator, an::LocalBackgroundEstimator::Percentile);
    EXPECT_DOUBLE_EQ(request.backgroundPercentile, 0.4);
    EXPECT_DOUBLE_EQ(request.sigmaClip, 2.5);
    EXPECT_EQ(request.sigmaClipIterations, 4u);
    EXPECT_EQ(request.backgroundSmoothingRadius, 2u);
    EXPECT_EQ(request.scaleMode, an::LocalScaleMode::PerChannel);
    EXPECT_EQ(request.localScaleSmoothingRadius, 3u);
}

// A Noise weighting weighs each frame by one over the square of the noise the stacking measured on its pixels;
// a set no frame of which has a measurable noise is stacked with equal weights and says so; and one only some frames of
// which have one is said by its first frame without it, for the stack to refuse as it refuses the other weightings'.
TEST_F(StackingTest, NoiseWeightsAreEachFramesNoiseOrEqualWeightsWhenNoFrameHasOne)
{
    const std::vector<StandaloneStackingFrame> frames{listedFrame(QStringLiteral("/frames/first.fits")),
                                                      listedFrame(QStringLiteral("/frames/second.fits")),
                                                      listedFrame(QStringLiteral("/frames/third.fits"))};

    const an::ui::StandaloneStackingWeightingResult measured =
        an::ui::standaloneNoiseWeights(frames, {0.01, 0.02, 0.04});
    EXPECT_EQ(measured.method, an::StackWeighting::Noise);
    ASSERT_EQ(measured.measurements.size(), 3u);
    const double expected[] = {0.01, 0.02, 0.04};
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        ASSERT_TRUE(measured.measurements[index].backgroundNoise.has_value()) << index;
        EXPECT_DOUBLE_EQ(*measured.measurements[index].backgroundNoise, expected[index]);
        EXPECT_FALSE(measured.measurements[index].dispersion.has_value()) << "a Noise weight reads the noise alone";
    }
    EXPECT_FALSE(measured.usedFallback);
    EXPECT_FALSE(measured.partlyMeasured);
    EXPECT_TRUE(measured.warning.isEmpty()) << measured.warning.toStdString();

    const an::ui::StandaloneStackingWeightingResult none =
        an::ui::standaloneNoiseWeights(frames, {std::nullopt, std::nullopt, 0.0});
    EXPECT_EQ(none.method, an::StackWeighting::Uniform);
    EXPECT_TRUE(none.usedFallback);
    EXPECT_TRUE(none.measurements.empty());
    EXPECT_TRUE(none.warning.contains(QStringLiteral("background-noise"))) << none.warning.toStdString();

    const an::ui::StandaloneStackingWeightingResult partly =
        an::ui::standaloneNoiseWeights(frames, {0.01, std::nullopt, 0.04});
    EXPECT_EQ(partly.method, an::StackWeighting::Noise);
    EXPECT_TRUE(partly.partlyMeasured);
    EXPECT_TRUE(partly.warning.contains(QStringLiteral("second.fits"))) << partly.warning.toStdString();
}

// A Noise-weighted average weighs each frame by one over the square of the noise measured on its pixels: three frames
// of 0.2, 0.4 and 0.6 whose noise is 0.002, 0.02 and 0.04 average near the quietest one's level, where equal weights
// would give 0.4.
TEST_F(StackingTest, ANoiseWeightedAverageWeighsEachFrameByItsMeasuredNoise)
{
    const QStringList frames = writeNoisy();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.weighting = QStringLiteral("Noise");
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    ASSERT_EQ(run.stacking.weighting, QStringLiteral("Noise"));
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("noise.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    EXPECT_TRUE(stacked->weightingWarning.isEmpty()) << stacked->weightingWarning.toStdString();
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    // The weights stand as one over 0.002 squared, 0.02 squared and 0.04 squared: 0.2 almost alone.
    const double mean = meanOf(*written);
    EXPECT_NEAR(mean, 0.203, 0.006);
    EXPECT_LT(mean, 0.25);
}

// Frames with no measurable noise - flat frames, whose rows differ by nothing - are stacked with equal weights, and the
// weights say so.
TEST_F(StackingTest, ANoiseWeightingOfFramesWithoutNoiseStacksWithEqualWeightsAndSaysSo)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.weighting = QStringLiteral("Noise");
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("flat_noise.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    EXPECT_TRUE(stacked->weightingWarning.contains(QStringLiteral("background-noise")))
        << stacked->weightingWarning.toStdString();
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->at(7u, 9u), 0.4, 1e-4);
}

// The window offers only what the run reads: a scale estimator for the scaling global normalizations alone - a local
// normalization's stack reads none - and its stored local settings carry nothing only the integration's analysis
// reads, since the stack fits each frame against its reference frame.
TEST_F(StackingTest, TheWindowOffersAndKeepsNoSettingTheRunDoesNotRead)
{
    StackingWindow window;
    auto& ui = window.window->ui();
    ui.normalizationMode->setCurrentText(QStringLiteral("Multiplicative With Scaling"));
    EXPECT_TRUE(ui.scaleEstimatorSelector->isEnabled());
    ui.normalizationMode->setCurrentText(QStringLiteral("Local"));
    EXPECT_FALSE(ui.scaleEstimatorSelector->isEnabled()) << "a local normalization reads no scale estimator";
    ui.normalizationMode->setCurrentText(QStringLiteral("Additive"));
    EXPECT_FALSE(ui.scaleEstimatorSelector->isEnabled());

    const nlohmann::json document = nlohmann::json::parse(window.window->serialize_json_settings());
    ASSERT_TRUE(document["params"].contains("localNormalization"));
    const nlohmann::json& local = document["params"]["localNormalization"];
    for (const char* inert : {"referenceStrategy", "maxIntegratedFrames", "evaluationCriteria", "scaleEvaluation",
                              "psfType", "moffatBeta", "growthFactor", "maxStars", "minDetectionSnr",
                              "allowClusteredSources", "hotPixelMedianRadius", "noiseReductionRadius"})
    {
        EXPECT_FALSE(local.contains(inert)) << inert;
    }
}

// A master on disk always has its rejection maps beside it: the maps are written first and the master last, and a map
// that cannot be written leaves neither.
TEST_F(StackingTest, AMasterIsNeverLeftWithoutItsRejectionMaps)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.createRejectionMaps = true;
    run.stacking.mergeRejectionMaps = true;
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    ASSERT_TRUE(run.stacking.createRejectionMaps);
    run.outputDirectory = output();
    const QString master = QDir(output()).filePath(QStringLiteral("blocked.fit"));
    // A directory where the merged map would go, so the map cannot be written.
    ASSERT_TRUE(QDir().mkpath(an::ui::processes::standaloneRejectionMapPath(master, QStringLiteral("rej_l+h"))));

    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("blocked.fit")), run, {}, {});
    EXPECT_FALSE(stacked.hasValue());
    EXPECT_FALSE(QFileInfo::exists(master)) << "a master was left without its maps";

    const an::Result<an::ui::processes::StackedGroup> whole = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("whole.fit")), run, {}, {});
    ASSERT_TRUE(whole.hasValue()) << messageChain(whole.status());
    ASSERT_EQ(whole->files.size(), 2u);
    EXPECT_EQ(QFileInfo(whole->files.front().path).fileName(), QStringLiteral("whole.fit"))
        << "the stacked image comes first";
    for (const an::ui::processes::StackedFile& file : whole->files)
    {
        EXPECT_TRUE(QFileInfo::exists(file.path)) << file.path.toStdString();
    }
}

// Each rejection map a stack writes is described by the map it is - low, high or merged - whatever the path it is
// written to holds: a high map written below a directory whose name holds "_rej_l." is still the high one.
TEST_F(StackingTest, EachRejectionMapIsDescribedByTheMapItIsWhateverItsDirectoryIsCalled)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.createRejectionMaps = true;
    run.stacking.mergeRejectionMaps = false;
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    ASSERT_TRUE(run.stacking.createRejectionMaps);
    run.outputDirectory = QDir(output()).filePath(QStringLiteral("night_rej_l.maps"));
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("described.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    ASSERT_EQ(stacked->files.size(), 3u);
    EXPECT_EQ(stacked->files[1].description, QStringLiteral("Low rejection map"))
        << stacked->files[1].path.toStdString();
    EXPECT_EQ(stacked->files[2].description, QStringLiteral("High rejection map"))
        << stacked->files[2].path.toStdString();

    run.stacking.mergeRejectionMaps = true;
    const an::Result<an::ui::processes::StackedGroup> merged = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("merged.fit")), run, {}, {});
    ASSERT_TRUE(merged.hasValue()) << messageChain(merged.status());
    ASSERT_EQ(merged->files.size(), 2u);
    EXPECT_EQ(merged->files[1].description, QStringLiteral("Merged rejection map"));
}

// What the engine warns of a stack is returned with the group and logged as it is said: a sum no element of which is
// positive is written as it is.
TEST_F(StackingTest, AStackSaysWhatTheEngineWarnedAndLogsIt)
{
    const QStringList frames = writeZeros();
    StandaloneStackRun run;
    run.stacking.stackMethod = QStringLiteral("Sum");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    run.outputDirectory = output();
    const LogCapture log;
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("sum.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    EXPECT_TRUE(stacked->warnings.join(QLatin1Char(' ')).contains(QStringLiteral("Sum not normalized")))
        << stacked->warnings.join(QStringLiteral("; ")).toStdString();
    EXPECT_GE(log.count("Sum not normalized"), 1u);
    EXPECT_EQ(stacked->files.size(), 1u);
}

// An X-Trans mosaic's stack is demosaiced under its own filter array when the pattern is the image's own, and a named
// two-by-two pattern over it is refused by name before anything is written.
TEST_F(StackingTest, AnXTransMosaicResultIsDemosaicedUnderItsOwnFilterArray)
{
    QStringList frames;
    for (int index = 0; index < 3; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("xtrans_%1.tif").arg(index));
        ASSERT_TRUE(an::tests::viewer::writeXTransTiff(session(), path));
        frames << path;
    }
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.debayer = true;
    run.cfaPattern = an::ui::bayerPatternChoiceFromText(an::ui::kBayerPatternFromImageText);
    run.outputDirectory = output();
    an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("xtrans.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->channels, 3u);

    run.cfaPattern = an::ui::bayerPatternChoiceFromText(QStringLiteral("RGGB"));
    stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("xtrans_rggb.fit")), run, {}, {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_NE(messageChain(stacked.status()).find("X-Trans"), std::string::npos) << messageChain(stacked.status());
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("xtrans_rggb.fit"))));
}

TEST_F(StackingTest, AGroupStacksIntoItsMeanAndStatesTheSummedExposure)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.sumExposureTimes = true;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("stacked_L.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
    EXPECT_EQ(stacked->frames, 3u);
    ASSERT_EQ(stacked->files.size(), 1u);
    EXPECT_EQ(stacked->files.front().path, QDir(output()).filePath(QStringLiteral("stacked_L.fit")));
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->type, an::SampleType::Float32);
    EXPECT_NEAR(written->at(7u, 9u), 0.4, 1e-4);
    const std::optional<double> exposure = written->number("EXPTIME");
    ASSERT_TRUE(exposure.has_value()) << written->card("EXPTIME").value_or("no EXPTIME card");
    EXPECT_NEAR(*exposure, 180.0, 1e-6);
}

// An exact zero where an alignment left a frame's border means nothing: the average there is the other frames' mean,
// and everywhere else all three frames'.
TEST_F(StackingTest, AFramesZeroBorderIsLeftOutOfTheAverage)
{
    QStringList frames;
    const double levels[] = {0.2, 0.4, 0.6};
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("bordered_%1.fits").arg(index));
        const double level = levels[index];
        // The first frame does not reach its eight leftmost columns.
        ASSERT_TRUE(writeFrame(session(), path, kSide, kSide, 1u, an::SampleType::Float32,
                               [index, level](std::uint32_t x, std::uint32_t, std::uint32_t) {
                                   return index == 0u && x < 8u ? 0.0 : level;
                               }));
        frames << path;
    }
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("bordered.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->at(20u, 20u), 0.4, 1e-6);
    EXPECT_NEAR(written->at(2u, 20u), 0.5, 1e-6) << "the border is no sample, so the other two frames are averaged";
}

// An exact zero where an alignment left a frame's border is not judged by a rejection: it is no sample, so no
// rejection map counts it and the pixel is the other frames' mean.
TEST_F(StackingTest, AFramesZeroBorderIsNotJudgedByTheRejection)
{
    QStringList frames;
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("bordered_rej_%1.fits").arg(index));
        // The third frame does not reach its eight leftmost columns.
        ASSERT_TRUE(writeFrame(session(), path, kSide, kSide, 1u, an::SampleType::Float32,
                               [index](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                   const double sky = 0.3 + 0.01 * static_cast<double>((x + y + index) % 3u);
                                   return index == 2u && x < 8u ? 0.0 : sky;
                               }));
        frames << path;
    }
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    run.stacking.sigmaLow = 2.0;
    run.stacking.sigmaHigh = 1.5;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.createRejectionMaps = true;
    run.stacking.mergeRejectionMaps = true;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("bordered_rej.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    ASSERT_EQ(stacked->files.size(), 2u);
    const std::optional<ReadBack> map = readBack(session(), stacked->files[1].path);
    ASSERT_TRUE(map.has_value());
    EXPECT_NEAR(map->at(2u, 20u), 0.0, 1e-6) << "the border is no sample, so no rejection counts it";
    const std::optional<ReadBack> stack = readBack(session(), stacked->files[0].path);
    ASSERT_TRUE(stack.has_value());
    double sum = 0.0;
    for (std::uint32_t index : {0u, 1u, 3u, 4u})
    {
        sum += 0.3 + 0.01 * static_cast<double>((2u + 20u + index) % 3u);
    }
    EXPECT_NEAR(stack->at(2u, 20u), sum / 4.0, 1e-6);
}

// A sample that is not a number means nothing: the average there is the other frames' mean.
TEST_F(StackingTest, ASampleThatIsNotANumberIsLeftOutOfTheAverage)
{
    QStringList frames;
    const double levels[] = {0.2, 0.4, 0.6};
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("dead_%1.fits").arg(index));
        const double level = levels[index];
        // The first frame is not a number at one sample, as a calibration leaves it where its flat is zero.
        ASSERT_TRUE(writeFrame(session(), path, kSide, kSide, 1u, an::SampleType::Float32,
                               [index, level](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                   return index == 0u && x == 5u && y == 6u ? std::numeric_limits<double>::quiet_NaN()
                                                                            : level;
                               }));
        frames << path;
    }
    const std::optional<ReadBack> dead = readBack(session(), frames.front());
    ASSERT_TRUE(dead.has_value());
    ASSERT_TRUE(std::isnan(dead->at(5u, 6u))) << "the frame keeps the sample that is not a number";
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("dead.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << messageChain(stacked.status());
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->at(20u, 20u), 0.4, 1e-6);
    EXPECT_NEAR(written->at(5u, 6u), 0.5, 1e-6);
}

TEST_F(StackingTest, AStackStatesItsExposureAsANumberWhateverTheProcessLocale)
{
    const CommaDecimalLocale locale;
    if (!locale.active())
    {
        GTEST_SKIP() << "the host installs no locale with a decimal comma";
    }
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.sumExposureTimes = true;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("locale.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    const std::optional<double> exposure = written->number("EXPTIME");
    ASSERT_TRUE(exposure.has_value()) << written->card("EXPTIME").value_or("no EXPTIME card");
    EXPECT_NEAR(*exposure, 180.0, 1e-6);
}

TEST_F(StackingTest, AMedianOfSixtyFourBitsIsWrittenAsFloat64)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.stackMethod = QStringLiteral("Median");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(run.stacking);
    run.output64 = true;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("median.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->type, an::SampleType::Float64);
    EXPECT_NEAR(written->at(0u, 0u), 0.4, 1e-4);
    // Without the sum the exposure is the reference frame's.
    const std::optional<double> exposure = written->number("EXPTIME");
    ASSERT_TRUE(exposure.has_value()) << written->card("EXPTIME").value_or("no EXPTIME card");
    EXPECT_NEAR(*exposure, 60.0, 1e-6);
}

TEST_F(StackingTest, RejectionMapsAreWrittenAsFractionsOfTheFrames)
{
    // Five frames of one value but for an outlier in the third at one pixel.
    QStringList frames;
    for (int index = 0; index < 5; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("rej_%1.fits").arg(index));
        ASSERT_TRUE(writeFrame(session(), path, kSide, kSide, 1u, an::SampleType::Float32,
                               [index](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                   const double sky = 0.3 + 0.01 * static_cast<double>((x + y + index) % 3);
                                   return index == 2 && x == 10u && y == 10u ? 0.95 : sky;
                               }));
        frames << path;
    }
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    run.stacking.sigmaLow = 2.0;
    run.stacking.sigmaHigh = 1.5;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.stacking.createRejectionMaps = true;
    run.stacking.mergeRejectionMaps = true;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("rejected.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
    ASSERT_EQ(stacked->files.size(), 2u);
    EXPECT_EQ(stacked->files[1].path, QDir(output()).filePath(QStringLiteral("rejected_rej_l+h.fit")));
    const std::optional<ReadBack> map = readBack(session(), stacked->files[1].path);
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->type, an::SampleType::Float32);
    EXPECT_NEAR(map->at(10u, 10u), 1.0 / 5.0, 1e-6);
    const std::optional<ReadBack> stack = readBack(session(), stacked->files[0].path);
    ASSERT_TRUE(stack.has_value());
    EXPECT_LT(stack->at(10u, 10u), 0.5);
}

TEST_F(StackingTest, AGroupWithFramesOfAnotherSizeIsRefusedNamingTheGroup)
{
    QStringList frames = writeThree();
    const QString odd = temporaryPath(QStringLiteral("odd.fits"));
    ASSERT_TRUE(writeFlatFrame(session(), odd, kSide / 2u, kSide, 1u, 0.4, an::SampleType::UInt16));
    frames << odd;
    StandaloneStackRun run;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("x.fit")), run, {}, {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_NE(std::string(stacked.status().message()).find("Group 'group'"), std::string::npos)
        << stacked.status().message();
    EXPECT_NE(std::string(stacked.status().message()).find("mismatched dimensions"), std::string::npos);
}

TEST_F(StackingTest, AGroupWithFramesOfAnotherHeightAloneIsRefused)
{
    QStringList frames = writeThree();
    const QString flat = temporaryPath(QStringLiteral("flat.fits"));
    ASSERT_TRUE(writeFlatFrame(session(), flat, kSide, kSide / 2u, 1u, 0.4, an::SampleType::UInt16));
    frames << flat;
    StandaloneStackRun run;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.outputDirectory = output();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("x.fit")), run, {}, {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_NE(std::string(stacked.status().message()).find("mismatched dimensions"), std::string::npos)
        << stacked.status().message();
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("x.fit"))));
}

TEST_F(StackingTest, AGroupMixingFilterArraysIsRefusedNamingTheGroup)
{
    const auto mosaic = [this](const QString& name, std::optional<an::CfaPattern> pattern) {
        const QString path = temporaryPath(name);
        std::optional<an::CfaDescriptor> cfa;
        if (pattern.has_value())
        {
            cfa = an::CfaDescriptor::create(*pattern, 0u, 0u);
        }
        EXPECT_TRUE(writeFrame(
            session(), path, kSide, kSide, 1u, an::SampleType::UInt16,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return 0.3 + 0.01 * static_cast<double>((x + y) % 2u);
            },
            {}, cfa));
        return path;
    };
    StandaloneStackRun run;
    run.stacking.rejectionMethod = QStringLiteral("None");
    run.stacking.normalizationMode = NormalizationMode::None;
    run.debayer = true;
    run.outputDirectory = output();
    const QStringList rggb{mosaic(QStringLiteral("rggb_0.fits"), an::CfaPattern::Rggb),
                           mosaic(QStringLiteral("rggb_1.fits"), an::CfaPattern::Rggb)};

    // A mosaic beside a frame that states no filter array.
    an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(),
        groupOf(session(), rggb + QStringList{mosaic(QStringLiteral("grey.fits"), std::nullopt)},
                QStringLiteral("mixed.fit")),
        run, {}, {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_EQ(stacked.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(stacked.status().message()).find("Group 'group'"), std::string::npos)
        << stacked.status().message();
    EXPECT_NE(std::string(stacked.status().message()).find("mismatched filter arrays"), std::string::npos)
        << stacked.status().message();

    // Two mosaics of different patterns.
    stacked = an::ui::processes::stackStandaloneGroup(
        session(),
        groupOf(session(), rggb + QStringList{mosaic(QStringLiteral("bggr.fits"), an::CfaPattern::Bggr)},
                QStringLiteral("patterns.fit")),
        run, {}, {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_NE(std::string(stacked.status().message()).find("mismatched filter arrays"), std::string::npos)
        << stacked.status().message();
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("patterns.fit"))));

    // Mosaics of one pattern stack.
    stacked = an::ui::processes::stackStandaloneGroup(
        session(),
        groupOf(session(), rggb + QStringList{mosaic(QStringLiteral("rggb_2.fits"), an::CfaPattern::Rggb)},
                QStringLiteral("same.fit")),
        run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
}

TEST_F(StackingTest, TheRejectionBoxesOfferOnlyWhatTheStackAccepts)
{
    StackingWindow window;
    auto& ui = window.window->ui();
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Average"));
    for (const QString& rejection : {QStringLiteral("Sigma Clipping"), QStringLiteral("Percentile"),
                                     QStringLiteral("Linear Fit"), QStringLiteral("GESDT")})
    {
        ui.rejectionMethodSelector->setCurrentText(rejection);
        ASSERT_EQ(ui.rejectionMethodSelector->currentText(), rejection);
        // No rejection accepts a threshold of zero.
        ui.sigmaLowValue->setValue(0.0);
        ui.sigmaHighValue->setValue(0.0);
        EXPECT_GT(ui.sigmaLowValue->value(), 0.0) << rejection.toStdString();
        EXPECT_GT(ui.sigmaHighValue->value(), 0.0) << rejection.toStdString();
        StackingSettings settings = window.window->runSettings().stacking;
        an::Result<an::StackRequest> request = requestOf(session(), settings);
        EXPECT_TRUE(request.hasValue()) << rejection.toStdString() << ": " << request.status().message();
    }
    // The generalized ESD test reads both numbers strictly between zero and one.
    ui.sigmaLowValue->setValue(5.0);
    ui.sigmaHighValue->setValue(1.0);
    EXPECT_LT(ui.sigmaLowValue->value(), 1.0);
    EXPECT_LT(ui.sigmaHighValue->value(), 1.0);
    an::Result<an::StackRequest> request = requestOf(session(), window.window->runSettings().stacking);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    // A sigma rejection takes thresholds above one again.
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("Sigma Clipping"));
    ui.sigmaLowValue->setValue(5.0);
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->value(), 5.0);
}

// Percentile rejects a sample further below the median than its low fraction of the median, or further above than its
// high fraction: choosing it starts at the engine's 0.2 and 0.1, and its controls say what they are.
TEST_F(StackingTest, PercentileStartsAtTheEnginesFractionsOfTheMedian)
{
    StackingWindow window;
    auto& ui = window.window->ui();
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Average"));
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("Percentile"));
    ASSERT_EQ(ui.rejectionMethodSelector->currentText(), QStringLiteral("Percentile"));
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->value(), 0.2);
    EXPECT_DOUBLE_EQ(ui.sigmaHighValue->value(), 0.1);
    EXPECT_EQ(ui.sigmaLowLabel->text(), QStringLiteral("Low Fraction of Median"));
    EXPECT_EQ(ui.sigmaHighLabel->text(), QStringLiteral("High Fraction of Median"));
    EXPECT_TRUE(ui.rejectionMethodSelector->toolTip().contains(QStringLiteral("fraction of the median")))
        << ui.rejectionMethodSelector->toolTip().toStdString();
    const StackingSettings settings = window.window->runSettings().stacking;
    EXPECT_DOUBLE_EQ(settings.sigmaLow, 0.2);
    EXPECT_DOUBLE_EQ(settings.sigmaHigh, 0.1);
}

TEST_F(StackingTest, AStoppedGroupLeavesNoFile)
{
    const QStringList frames = writeThree();
    StandaloneStackRun run;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.outputDirectory = output();
    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("x.fit")), run, stop.get_token(), {});
    ASSERT_FALSE(stacked.hasValue());
    EXPECT_EQ(stacked.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("x.fit"))));
}

TEST_F(StackingTest, AColourMosaicResultIsDemosaicedWhenAsked)
{
    QStringList frames;
    for (int index = 0; index < 3; ++index)
    {
        const QString path = temporaryPath(QStringLiteral("cfa_%1.fits").arg(index));
        ASSERT_TRUE(writeFlatFrame(session(), path, kSide, kSide, 1u, 0.3, an::SampleType::UInt16));
        frames << path;
    }
    StandaloneStackRun run;
    run.stacking.normalizationMode = NormalizationMode::None;
    run.debayer = true;
    run.cfaPattern = an::ui::bayerPatternChoiceFromText(QStringLiteral("RGGB"));
    run.outputDirectory = output();
    // A frame that states no filter array is not a mosaic, so the demosaic leaves the grey stack as it is.
    const an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
        session(), groupOf(session(), frames, QStringLiteral("grey.fit")), run, {}, {});
    ASSERT_TRUE(stacked.hasValue()) << stacked.status().message();
    const std::optional<ReadBack> written = readBack(session(), stacked->files.front().path);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->channels, 1u);
}

TEST_F(StackingTest, TheWindowGroupsByFilterAndNamesEachGroupsOutput)
{
    const QStringList red = writeThree(QStringLiteral("R"), QStringLiteral("r_"));
    const QStringList green = writeThree(QStringLiteral("G"), QStringLiteral("g_"));
    StackingWindow window;
    window.window->addItemsNonBlocking(red + green);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(window.window->groups().size(), 2u);
    QStringList names;
    for (const StandaloneStackingGroup& group : window.window->groups())
    {
        names << group.outputFileName;
        EXPECT_EQ(group.frameCount, 3);
    }
    names.sort();
    EXPECT_EQ(names, (QStringList{QStringLiteral("stacked_G.fit"), QStringLiteral("stacked_R.fit")}));
    window.window->ui().groupByFilter->setChecked(false);
    EXPECT_EQ(window.window->groups().size(), 1u);
}

TEST_F(StackingTest, TheWindowStacksEveryGroupAndOpensWhatItWrote)
{
    const QStringList frames = writeThree();
    StackingWindow window;
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    auto& ui = window.window->ui();
    ui.normalizationMode->setCurrentText(QStringLiteral("None"));
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("None"));
    ui.outputDirectoryEdit->setText(output());
    ASSERT_TRUE(ui.stackImagesButton->isEnabled());
    const std::size_t viewsBefore = context().madeViews.size();
    window.window->stack();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(window.window->lastWrittenFiles().size(), 1);
    EXPECT_EQ(QFileInfo(window.window->lastWrittenFiles().front()).fileName(), QStringLiteral("stacked_L.fit"));
    ASSERT_TRUE(pumpUntil([this, viewsBefore]() { return context().madeViews.size() == viewsBefore + 1u; }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString()
                                                << context().messages.join(QStringLiteral("; ")).toStdString();
}

// The window stacks a Noise-weighted average.
TEST_F(StackingTest, TheWindowStacksANoiseWeightedAverage)
{
    const QStringList frames = writeNoisy();
    StackingWindow window;
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    auto& ui = window.window->ui();
    ui.weightingSelector->setCurrentText(QStringLiteral("Noise"));
    ASSERT_EQ(ui.weightingSelector->currentText(), QStringLiteral("Noise"));
    ui.normalizationMode->setCurrentText(QStringLiteral("None"));
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("None"));
    ui.outputDirectoryEdit->setText(output());
    window.window->stack();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(window.window->lastWrittenFiles().size(), 1);
}

// When a stack the window ran warned of something, the window says so once the run has ended. What the engine said was
// neither shown nor logged.
TEST_F(StackingTest, TheWindowShowsWhatTheStackWarned)
{
    const QStringList frames = writeZeros();
    StackingWindow window;
    // The banner is part of the window's shell, which is laid out when the window is first shown.
    window.window->show();
    QCoreApplication::processEvents();
    ASSERT_NE(window.window->findChild<QLabel*>(QStringLiteral("anProcessShellBannerMessage")), nullptr);
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    auto& ui = window.window->ui();
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Sum"));
    ASSERT_EQ(ui.stackMethodSelector->currentText(), QStringLiteral("Sum"));
    ui.normalizationMode->setCurrentText(QStringLiteral("None"));
    ui.outputDirectoryEdit->setText(output());
    window.window->stack();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_EQ(window.window->lastWrittenFiles().size(), 1);
    auto* label = window.window->findChild<QLabel*>(QStringLiteral("anProcessShellBannerMessage"));
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->text().contains(QStringLiteral("Sum not normalized")))
        << "the banner reads '" << label->text().toStdString() << "'";
}

// Exposure weighting reads each frame's exposure from its header before the stack runs, so the window's weighting
// banner says beforehand - with no output directory chosen, since nothing is read from one - that no frame states an
// exposure and the stack will use equal weights, and, once one frame does, that the others lack it. The banner left
// Exposure weighting out, so the window said nothing of it until the run had ended.
TEST_F(StackingTest, TheWeightingBannerSaysBeforeTheRunWhichFramesStateNoExposure)
{
    QStringList frames;
    for (const double value : {0.2, 0.4, 0.6})
    {
        const QString path = temporaryPath(QStringLiteral("unexposed_%1.fits").arg(static_cast<int>(value * 10.0)));
        ASSERT_TRUE(writeFlatFrame(session(), path, kSide, kSide, 1u, value, an::SampleType::UInt16));
        frames << path;
    }
    StackingWindow window;
    window.window->show();
    QCoreApplication::processEvents();
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    auto& ui = window.window->ui();
    ui.outputDirectoryEdit->clear();
    ui.weightingSelector->setCurrentText(QStringLiteral("Exposure"));
    ASSERT_EQ(ui.weightingSelector->currentText(), QStringLiteral("Exposure"));
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_FALSE(ui.weightingBanner->isHidden()) << "the banner said nothing of Exposure weighting";
    EXPECT_TRUE(ui.weightingBanner->message().contains(QStringLiteral("Exposure weighting could not derive")))
        << "the banner reads '" << ui.weightingBanner->message().toStdString() << "'";

    const QString exposed = temporaryPath(QStringLiteral("exposed.fits"));
    ASSERT_TRUE(writeFlatFrame(session(), exposed, kSide, kSide, 1u, 0.3, an::SampleType::UInt16, 60.0));
    window.window->addItemsNonBlocking({exposed});
    ASSERT_TRUE(
        pumpUntil([&window]() { return window.window->fileList().paths().size() == 4 && window.window->isIdle(); }));
    EXPECT_FALSE(ui.weightingBanner->isHidden());
    EXPECT_TRUE(ui.weightingBanner->message().contains(QStringLiteral("missing usable exposure figures")))
        << "the banner reads '" << ui.weightingBanner->message().toStdString() << "'";
}

TEST_F(StackingTest, TheWindowReportsACometModeWithNoRegistrationRun)
{
    const QStringList frames = writeThree();
    StackingWindow window;
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->ui().cometStackingModeSelector->setCurrentText(QStringLiteral("Comet"));
    window.window->ui().outputDirectoryEdit->setText(output());
    window.window->stack();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(context().errorTexts.size(), 1);
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("registration_metrics.db")));
    EXPECT_TRUE(window.window->lastWrittenFiles().isEmpty());
}

TEST_F(StackingTest, TheLocalNormalizationDialogIsTheIntegrationsAndItsAnswerIsKept)
{
    StackingWindow window;
    window.window->ui().normalizationMode->setCurrentText(QStringLiteral("Local"));
    EXPECT_FALSE(window.window->ui().localNormalizationConfigureButton->isHidden());
    window.window->ui().localNormalizationConfigureButton->click();
    LocalNormalizationSettingsDialog* dialog = nullptr;
    ASSERT_TRUE(
        pumpUntil([&dialog]() { return (dialog = shownDialog<LocalNormalizationSettingsDialog>()) != nullptr; }));
    // It leaves out what only the integration's analysis reads, and says why.
    auto* note = dialog->findChild<QLabel*>(QStringLiteral("referenceAnalysisNote"));
    ASSERT_NE(note, nullptr);
    EXPECT_FALSE(note->isHidden());
    EXPECT_TRUE(note->text().contains(QStringLiteral("against its reference frame"))) << note->text().toStdString();
    LocalNormalizationSettings answer = dialog->getSettings();
    EXPECT_EQ(answer.gridSize, window.window->localNormalizationSettings().gridSize)
        << "it opens over the window's settings";
    answer.gridSize = 9;
    dialog->setSettings(answer);
    dialog->accept();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->localNormalizationSettings().gridSize == 9; }));
}

// Every local normalization field the window's dialog shows survives a save and reload. The dialog shows the twelve the
// stack's local fit reads and hides what only Image Integration's analysis reads, which the window does not keep, so a
// control shown there and not saved would be lost on the next reload.
TEST_F(StackingTest, EveryLocalNormalizationFieldTheDialogShowsSurvivesASaveAndReload)
{
    LocalNormalizationSettings answer;
    answer.gridSize = 9;
    answer.gridInterpolation = QStringLiteral("Bilinear");
    answer.backgroundSmoothingRadius = 2;
    answer.scaleMode = QStringLiteral("Per-Channel");
    answer.lowClip = 1.5e-4;
    answer.highClip = 0.75;
    answer.backgroundEstimator = QStringLiteral("Percentile");
    answer.backgroundPercentile = 0.3;
    answer.sigmaClipK = 2.5;
    answer.sigmaClipIterations = 5;
    answer.localScaleEnabled = true;
    answer.localScaleSmoothingRadius = 4;

    std::string stored;
    {
        StackingWindow window;
        window.window->ui().normalizationMode->setCurrentText(QStringLiteral("Local"));
        window.window->ui().localNormalizationConfigureButton->click();
        LocalNormalizationSettingsDialog* dialog = nullptr;
        ASSERT_TRUE(
            pumpUntil([&dialog]() { return (dialog = shownDialog<LocalNormalizationSettingsDialog>()) != nullptr; }));
        // Sections open collapsed; a control is offered when it shows with its section open.
        for (QWidget* widget : dialog->findChildren<QWidget*>())
        {
            if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
            {
                section->setExpanded(true);
            }
        }
        const auto shown = [dialog](const char* name) {
            const QWidget* control = dialog->findChild<QWidget*>(QString::fromLatin1(name));
            EXPECT_NE(control, nullptr) << name;
            return control != nullptr && control->isVisibleTo(dialog);
        };
        for (const char* name : {"gridSize", "gridInterpolation", "backgroundSmoothingRadius", "scaleMode", "lowClip",
                                 "highClip", "backgroundEstimator", "backgroundPercentile", "sigmaClipK",
                                 "sigmaClipIterations", "localScaleEnabled", "localScaleSmoothingRadius"})
        {
            EXPECT_TRUE(shown(name)) << name;
        }
        for (const char* name : {"referenceStrategy", "maxIntegratedFrames", "evaluationCriteria", "scaleEvaluation",
                                 "psfType", "moffatBeta", "maxStars", "minDetectionSnr", "allowClusteredSources",
                                 "hotPixelMedianRadius", "noiseReductionRadius"})
        {
            EXPECT_FALSE(shown(name)) << name;
        }
        dialog->setSettings(answer);
        dialog->accept();
        ASSERT_TRUE(pumpUntil([&window]() { return window.window->localNormalizationSettings().gridSize == 9; }));
        stored = window.window->serialize_json_settings();
    }

    StackingWindow restored;
    restored.window->deserialize_json_settings(stored);
    const LocalNormalizationSettings read = restored.window->localNormalizationSettings();
    EXPECT_EQ(read.gridSize, answer.gridSize);
    EXPECT_EQ(read.gridInterpolation, answer.gridInterpolation);
    EXPECT_EQ(read.backgroundSmoothingRadius, answer.backgroundSmoothingRadius);
    EXPECT_EQ(read.scaleMode, answer.scaleMode);
    EXPECT_DOUBLE_EQ(read.lowClip, answer.lowClip);
    EXPECT_DOUBLE_EQ(read.highClip, answer.highClip);
    EXPECT_EQ(read.backgroundEstimator, answer.backgroundEstimator);
    EXPECT_DOUBLE_EQ(read.backgroundPercentile, answer.backgroundPercentile);
    EXPECT_DOUBLE_EQ(read.sigmaClipK, answer.sigmaClipK);
    EXPECT_EQ(read.sigmaClipIterations, answer.sigmaClipIterations);
    EXPECT_EQ(read.localScaleEnabled, answer.localScaleEnabled);
    EXPECT_EQ(read.localScaleSmoothingRadius, answer.localScaleSmoothingRadius);
}

TEST_F(StackingTest, TheWindowPersistsEveryOptionAndItsFrames)
{
    const QStringList frames = writeThree();
    std::string stored;
    {
        StackingWindow window;
        window.window->addItemsNonBlocking(frames);
        ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
        auto& ui = window.window->ui();
        ui.stackMethodSelector->setCurrentText(QStringLiteral("Average"));
        ui.rejectionMethodSelector->setCurrentText(QStringLiteral("GESDT"));
        ui.sigmaLowValue->setValue(0.25);
        ui.sigmaHighValue->setValue(0.04);
        ui.weightingSelector->setCurrentText(QStringLiteral("Exposure"));
        ui.normalizationMode->setCurrentText(QStringLiteral("Multiplicative With Scaling"));
        ui.scaleEstimatorSelector->setCurrentText(QStringLiteral("MAD"));
        ui.groupBySession->setChecked(true);
        ui.groupByExposure->setChecked(true);
        ui.exposureTolerancePercentValue->setValue(12.0);
        ui.outputDirectoryEdit->setText(output());
        ui.outputImageNameEdit->setText(QStringLiteral("m_$filter_$exposure.fits"));
        ui.openAfterStackCheck->setChecked(false);
        ui.sumExposureTimesCheckBox->setChecked(true);
        ui.process64BitsImageCheckbox->setChecked(true);
        ui.createRejectionMapsCheck->setChecked(true);
        ui.mergeRejectionMapsCheck->setChecked(false);
        ui.limitMemoryUsageGroupBox->setChecked(false);
        ui.maxMemoryMBValue->setValue(2048);
        ui.debayerCFAGroupBox->setChecked(true);
        ui.cfaPattern->setCurrentText(QStringLiteral("GBRG"));
        ui.debayerMethod->setCurrentText(QStringLiteral("AMaZE"));
        stored = window.window->serialize_json_settings();
    }
    const nlohmann::json document = nlohmann::json::parse(stored);
    EXPECT_EQ(document.value("version", 0), 5);
    EXPECT_EQ(document["params"].value("rejectionMethod", std::string()), "GESDT");
    EXPECT_EQ(document["params"].value("debayerMethod", std::string()), "AMaZE");
    EXPECT_TRUE(document["params"].contains("localNormalization"));

    StackingWindow restored;
    restored.window->deserialize_json_settings(stored);
    ASSERT_TRUE(pumpUntil([&restored]() { return restored.window->isIdle(); }));
    EXPECT_EQ(restored.window->fileList().paths().size(), 3);
    const StandaloneStackRun run = restored.window->runSettings();
    EXPECT_EQ(run.stacking.rejectionMethod, QStringLiteral("GESDT"));
    EXPECT_DOUBLE_EQ(run.stacking.sigmaLow, 0.25);
    EXPECT_DOUBLE_EQ(run.stacking.sigmaHigh, 0.04);
    EXPECT_EQ(run.stacking.weighting, QStringLiteral("Exposure"));
    EXPECT_EQ(run.stacking.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(run.stacking.globalNormalizationMethod, GlobalNormalizationMethod::MultiplicativeScaling);
    EXPECT_EQ(run.stacking.scaleEstimator, ScaleEstimatorMethod::MAD);
    EXPECT_TRUE(run.stacking.createRejectionMaps);
    EXPECT_FALSE(run.stacking.mergeRejectionMaps);
    EXPECT_FALSE(run.stacking.limitMemory);
    EXPECT_EQ(run.stacking.maxMemoryMb, 2048);
    EXPECT_TRUE(run.output64);
    EXPECT_TRUE(run.sumExposureTimes);
    EXPECT_TRUE(run.debayer);
    ASSERT_TRUE(run.cfaPattern.stated.has_value());
    EXPECT_EQ(*run.cfaPattern.stated, an::CfaPattern::Gbrg);
    EXPECT_EQ(run.demosaicMethod, an::DemosaicMethod::Amaze);
    EXPECT_EQ(run.outputDirectory, output());
    auto& ui = restored.window->ui();
    EXPECT_TRUE(ui.groupBySession->isChecked());
    EXPECT_TRUE(ui.groupByExposure->isChecked());
    EXPECT_DOUBLE_EQ(ui.exposureTolerancePercentValue->value(), 12.0);
    EXPECT_EQ(ui.outputImageNameEdit->text(), QStringLiteral("m_$filter_$exposure.fits"));
    EXPECT_FALSE(ui.openAfterStackCheck->isChecked());
}

TEST_F(StackingTest, AnUnofferedDemosaicMethodPersistedReadsAsRcd)
{
    StackingWindow window;
    window.window->deserialize_json_settings(R"({"version":5,"params":{"debayerMethod":"DCB"}})");
    EXPECT_EQ(window.window->ui().debayerMethod->currentText(), QStringLiteral("RCD"));
    EXPECT_EQ(window.window->ui().debayerMethod->findText(QStringLiteral("DCB")), -1);
}

} // namespace
} // namespace an::tests::processes::workflow
