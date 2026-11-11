// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The facts a report model states beyond what the report always stated: what one read of each image measures and
/// how often an image is read, what the calibration masters measure and who uses them, the groups of lights and the
/// lineage, what a registration group recorded, the stages a run did not time, its resources, and what the model
/// could not state and why.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportModelJson.h"
#include "processes/ImageIntegration/reporting/WorkflowReportArchiveLoader.h"
#include "processes/ImageIntegration/workflow/PreviewThumbnailGenerator.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
std::optional<ReportScenario> built(const ReportScenarioId id)
{
    std::optional<ReportScenario> scenario = buildReportScenario(id);
    EXPECT_TRUE(scenario.has_value()) << scenarioName(id);
    return scenario;
}

bool namesNotStated(const ReportModel& model, const QString& fragment)
{
    return std::any_of(model.notStated.begin(), model.notStated.end(), [&fragment](const ReportNotStated& entry) {
        return entry.what.contains(fragment) || entry.why.contains(fragment);
    });
}

const ReportCalibrationMaster* calibrationOf(const ReportModel& model, const ReportCalibrationKind kind,
                                             const QString& nameFragment = {})
{
    const auto found = std::find_if(
        model.calibrationMasters.begin(), model.calibrationMasters.end(), [&](const ReportCalibrationMaster& master) {
            return master.kind == kind && (nameFragment.isEmpty() || master.fileName.contains(nameFragment));
        });
    return found == model.calibrationMasters.end() ? nullptr : &*found;
}

/// A scenario's model built through the product's sources with the image reads counted.
struct CountedBuild
{
    ReportModel model;
    QHash<QString, int> reads;
};

CountedBuild buildCounting(const ReportScenario& scenario, const QString& runDirectory = {})
{
    const ProductionReportSources production(scenario.session(), scenario.metricsDatabasePath);
    ReportSources sources = production.sources();
    CountingImages counting(sources.images);
    sources.images = &counting;
    CountedBuild build;
    build.model = buildReportModel(ReportModelInputs{.manifest = scenario.manifest,
                                                     .result = scenario.result,
                                                     .runDirectory = runDirectory,
                                                     .context = scenario.context,
                                                     .counters = std::nullopt},
                                   sources);
    build.reads = counting.reads;
    return build;
}

/// Writes a model as the stored report of a run of the scenario's folder.
void storeModel(const ReportScenario& scenario, const QString& runId, const ReportModel& model)
{
    const QString folder = QDir(scenario.outputDirectory).filePath(QStringLiteral("history/%1").arg(runId));
    ASSERT_TRUE(QDir().mkpath(folder));
    QFile file(QDir(folder).filePath(QStringLiteral("workflow_report.json")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(ReportModelJson::toJson(model)).toJson(QJsonDocument::Compact));
}

int readsOf(const CountedBuild& build, const QString& path)
{
    return build.reads.value(QDir::cleanPath(QFileInfo(path).absoluteFilePath()), 0);
}
} // namespace

// ---- One read of each image.

TEST(ReportRunFacts, AMasterAndACalibrationMasterAreEachReadOnce)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const CountedBuild build = buildCounting(*scenario);

    EXPECT_EQ(build.reads.size(), scenario->facts.mastersMade + scenario->facts.calibrationMasters);
    for (auto read = build.reads.cbegin(); read != build.reads.cend(); ++read)
    {
        EXPECT_EQ(read.value(), 1) << read.key().toStdString();
    }
    for (const ReportMaster& master : build.model.masters)
    {
        EXPECT_EQ(readsOf(build, master.file), 1) << master.name.toStdString();
    }
    for (const ReportCalibrationMaster& master : build.model.calibrationMasters)
    {
        EXPECT_EQ(readsOf(build, master.file), 1) << master.fileName.toStdString();
    }
}

TEST(ReportRunFacts, AMastersDistributionAddsUpToItsSamples)
{
    for (const ReportScenarioId id :
         {ReportScenarioId::OneMonoMaster, ReportScenarioId::OneColourMaster, ReportScenarioId::DrizzledGroup})
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());

        const ReportModel model = buildScenarioModel(*scenario);

        for (const ReportMaster& master : model.masters)
        {
            SCOPED_TRACE(master.name.toStdString());
            ASSERT_TRUE(master.statistics.has_value());
            ASSERT_EQ(master.distribution.size(), master.statistics->channels);
            for (std::size_t channel = 0; channel < master.distribution.size(); ++channel)
            {
                const ReportSampleDistribution& distribution = master.distribution[channel];
                const ReportChannelStatistics& figures = master.statistics->channelStats[channel];
                EXPECT_DOUBLE_EQ(distribution.lowest, 1.0e-3);
                EXPECT_DOUBLE_EQ(distribution.highest, 1.0);
                ASSERT_EQ(distribution.counts.size(), 96u);
                const std::uint64_t binned =
                    std::accumulate(distribution.counts.begin(), distribution.counts.end(), std::uint64_t{0});
                EXPECT_EQ(binned + distribution.outside, figures.samples);
                EXPECT_EQ(figures.samples, master.statistics->width * master.statistics->height);

                // The bin the median falls in holds samples, and most of them lie around it.
                const double decades = std::log(distribution.highest / distribution.lowest);
                const auto bin =
                    static_cast<std::size_t>(96.0 * std::log(figures.median / distribution.lowest) / decades);
                ASSERT_LT(bin, 96u);
                std::uint64_t around = 0;
                for (std::size_t near = bin > 2u ? bin - 3u : 0u; near < (std::min)(bin + 4u, std::size_t{96}); ++near)
                {
                    around += distribution.counts[near];
                }
                EXPECT_GT(around, figures.samples / 2u);
            }
        }
    }
}

TEST(ReportRunFacts, WithoutASessionNothingIsMeasuredAndEachThingIsNamed)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersWithoutSession);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->session(), nullptr);

    const ReportModel model = buildScenarioModel(*scenario);

    int named = 0;
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.statistics.has_value());
        EXPECT_TRUE(master.distribution.empty());
        EXPECT_FALSE(master.solution.has_value());
        EXPECT_TRUE(namesNotStated(model, QStringLiteral("the master %1").arg(master.name)))
            << master.name.toStdString();
        ++named;
    }
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        EXPECT_TRUE(master.level.empty());
        EXPECT_TRUE(master.noise.empty());
        EXPECT_TRUE(namesNotStated(model, master.name)) << master.fileName.toStdString();
        ++named;
    }
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("headers")));
    const int measurements = static_cast<int>(
        std::count_if(model.notStated.begin(), model.notStated.end(), [](const ReportNotStated& entry) {
            // A master's figures and its spread; a calibration master's level and noise.
            return (entry.what.contains(QStringLiteral("statistics")) ||
                    entry.what.contains(QStringLiteral("level"))) &&
                   entry.why.contains(QStringLiteral("session"));
        }));
    EXPECT_EQ(measurements, named) << "one entry for each image that was not read";
    EXPECT_FALSE(model.run.headers.has_value());

    // What needs no image is still stated: the lights' metrics and what became of them.
    EXPECT_EQ(model.run.framesUsed.value_or(0), scenario->facts.lightsCombined);

    // With a session nothing of the kind is named.
    const auto withSession = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(withSession.has_value());
    const ReportModel measured = buildScenarioModel(*withSession);
    EXPECT_FALSE(namesNotStated(measured, QStringLiteral("session")));
}

TEST(ReportRunFacts, WithoutAMetricsDatabaseNoLightsMetricIsStatedAndTheModelSaysSo)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_TRUE(namesNotStated(model, QStringLiteral("no registration metrics database")));
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        EXPECT_FALSE(group.registered.has_value());
        EXPECT_FALSE(group.failed.has_value());
        EXPECT_TRUE(group.reference.isEmpty());
    }
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.photometry.has_value());
        for (const ReportFrame& frame : master.frames)
        {
            EXPECT_FALSE(frame.shiftX.has_value());
            EXPECT_FALSE(frame.backgroundNoise.has_value());
        }
    }
    // The masters are still measured, and the lights the stacks name are still the ones combined.
    EXPECT_EQ(model.run.framesUsed.value_or(0), scenario->facts.lightsCombined);
}

// ---- The calibration masters, the groups of lights and the lineage.

TEST(ReportRunFacts, ACalibrationMasterStatesItsLevelItsNoiseItsExposureAndWhoUsesIt)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportCalibrationMaster* bias = calibrationOf(model, ReportCalibrationKind::Bias);
    const ReportCalibrationMaster* dark = calibrationOf(model, ReportCalibrationKind::Dark, QStringLiteral("64x48"));
    const ReportCalibrationMaster* narrowDark =
        calibrationOf(model, ReportCalibrationKind::Dark, QStringLiteral("48x36"));
    const ReportCalibrationMaster* flat = calibrationOf(model, ReportCalibrationKind::Flat, QStringLiteral("Red"));
    ASSERT_NE(bias, nullptr);
    ASSERT_NE(dark, nullptr);
    ASSERT_NE(narrowDark, nullptr);
    ASSERT_NE(flat, nullptr);

    // The levels the masters were written at, within the scatter of their samples.
    ASSERT_EQ(bias->level.size(), 1u);
    EXPECT_NEAR(bias->level.front(), 0.02, 0.02 * 0.01);
    ASSERT_EQ(dark->level.size(), 1u);
    EXPECT_NEAR(dark->level.front(), 0.025, 0.025 * 0.01);
    ASSERT_EQ(flat->level.size(), 1u);
    EXPECT_NEAR(flat->level.front(), 0.45, 0.45 * 0.01);
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        ASSERT_EQ(master.noise.size(), master.level.size()) << master.fileName.toStdString();
        EXPECT_GT(master.noise.front(), 0.0);
        EXPECT_TRUE(master.measuredByRun.isEmpty()) << "this run measured it";
        ASSERT_TRUE(master.measuredFraction.has_value());
        EXPECT_DOUBLE_EQ(*master.measuredFraction, 1.0) << "a small master is measured whole";
        ASSERT_TRUE(master.bytes.has_value());
        EXPECT_EQ(*master.bytes, QFileInfo(master.file).size());
        EXPECT_TRUE(master.modifiedAt.has_value());
    }
    ASSERT_TRUE(dark->exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*dark->exposureSeconds, 120.0);
    EXPECT_FALSE(bias->exposureSeconds.has_value());

    // The broadband dark calibrates the four broadband masters, the other dark the hydrogen-alpha one, each flat its
    // filter's; the bias calibrates no light itself, and the flats name it.
    EXPECT_EQ(dark->usedBy.size(), 4);
    EXPECT_EQ(narrowDark->usedBy, QStringList{masterOfFilter(model, QStringLiteral("Ha"))->key});
    EXPECT_EQ(flat->usedBy, QStringList{masterOfFilter(model, QStringLiteral("Red"))->key});
    EXPECT_TRUE(bias->usedBy.isEmpty());
    EXPECT_EQ(flat->biasMasterSlug, bias->slug);
    EXPECT_TRUE(flat->darkMasterSlug.isEmpty());
}

TEST(ReportRunFacts, ALargeCalibrationMasterIsMeasuredOverTheCentreOfItsFrame)
{
    auto scenario = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(scenario.has_value());
    const QString dark = scenario->manifest.darkStage->groups.front().outputFilename;
    const QString path = QDir(scenario->outputDirectory).filePath(dark);
    // A frame of more pixels than a level is measured over, its border at another level than the rest.
    constexpr std::uint32_t kWidth = 2600u;
    constexpr std::uint32_t kHeight = 1600u;
    const SampleRule rule = [](const std::uint32_t x, const std::uint32_t y, std::uint32_t) {
        const bool border = x < 10u || y < 10u || x >= kWidth - 10u || y >= kHeight - 10u;
        return border ? 0.1 : 0.5 + 0.002 * static_cast<double>((x * 7u + y * 3u) % 5u);
    };
    ASSERT_TRUE(writeFloatImage(path, static_cast<int>(kWidth), static_cast<int>(kHeight),
                                an::tests::fixtures::FixtureChannels::Mono, 0.5, rule, {}));

    const CountedBuild build = buildCounting(*scenario);

    const ReportCalibrationMaster* master = calibrationOf(build.model, ReportCalibrationKind::Dark);
    ASSERT_NE(master, nullptr);
    ASSERT_TRUE(master->measuredFraction.has_value());
    EXPECT_LT(*master->measuredFraction, 1.0);
    EXPECT_NEAR(*master->measuredFraction, 4.0e6 / (2600.0 * 1600.0), 0.002);
    ASSERT_EQ(master->level.size(), 1u);
    // The border is outside the rectangle measured: the level is that of the centre alone.
    EXPECT_NEAR(master->level.front(), 0.504, 1e-4);
    EXPECT_GT(master->noise.front(), 0.0);
    EXPECT_EQ(readsOf(build, path), 1);
}

TEST(ReportRunFacts, ACalibrationMasterAnEarlierRunMeasuredIsNotReadAgainWhileItsFileIsTheSame)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel earlier = buildScenarioModel(*scenario);
    const QString earlierRun = QStringLiteral("20260301_000000_000");
    storeModel(*scenario, earlierRun, earlier);

    const CountedBuild build = buildCounting(*scenario, scenario->runDirectory);

    EXPECT_EQ(build.reads.size(), scenario->facts.mastersMade) << "only the masters are read";
    ASSERT_EQ(build.model.calibrationMasters.size(), earlier.calibrationMasters.size());
    for (std::size_t i = 0; i < earlier.calibrationMasters.size(); ++i)
    {
        const ReportCalibrationMaster& master = build.model.calibrationMasters[i];
        EXPECT_EQ(readsOf(build, master.file), 0) << master.fileName.toStdString();
        EXPECT_EQ(master.level, earlier.calibrationMasters[i].level);
        EXPECT_EQ(master.noise, earlier.calibrationMasters[i].noise);
        EXPECT_EQ(master.measuredByRun, earlierRun);
    }

    // A file that changed since is another file: it is read.
    const ReportCalibrationMaster* bias = calibrationOf(earlier, ReportCalibrationKind::Bias);
    ASSERT_NE(bias, nullptr);
    {
        QFile file(bias->file);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(bias->modifiedAt->addSecs(90), QFileDevice::FileModificationTime));
    }
    const CountedBuild again = buildCounting(*scenario, scenario->runDirectory);
    EXPECT_EQ(again.reads.size(), scenario->facts.mastersMade + 1);
    EXPECT_EQ(readsOf(again, bias->file), 1);
    const ReportCalibrationMaster* measured = calibrationOf(again.model, ReportCalibrationKind::Bias);
    EXPECT_TRUE(measured->measuredByRun.isEmpty());
    EXPECT_EQ(measured->level, bias->level);
}

TEST(ReportRunFacts, AReusedMasterStatesWhatTheRunThatMadeItStatedWhenTheHistoryKeepsThatReport)
{
    const auto scenario = built(ReportScenarioId::IncrementalRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->facts.mastersReused, 3);
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const QString earlierRun = QFileInfo(scenario->earlierRunDirectories.front()).fileName();

    // An earlier run whose archive keeps no report model: each reused master is read once, and the model says what
    // it cannot state of it.
    const QString storedReport =
        QDir(scenario->earlierRunDirectories.front()).filePath(QStringLiteral("workflow_report.json"));
    ASSERT_TRUE(QFile::remove(storedReport)) << "the earlier run stored its report, as a run does";
    const CountedBuild fromFiles = buildCounting(*scenario, scenario->runDirectory);
    EXPECT_EQ(fromFiles.reads.size(),
              scenario->facts.mastersMade + scenario->facts.mastersReused + scenario->facts.calibrationMasters);
    for (const ReportMaster& master : fromFiles.model.masters)
    {
        if (master.state != ReportMasterState::Reused)
        {
            continue;
        }
        EXPECT_TRUE(master.madeByRun.isEmpty());
        EXPECT_TRUE(master.statistics.has_value()) << "read from the file";
        EXPECT_FALSE(master.stack.has_value());
        EXPECT_TRUE(namesNotStated(fromFiles.model, QStringLiteral("How the master %1 was stacked").arg(master.name)));
    }

    // With that run's report kept, what it stated of the masters it made is this run's account of them.
    ReportModel stored = fromFiles.model;
    for (ReportMaster& master : stored.masters)
    {
        if (master.state != ReportMasterState::Reused)
        {
            continue;
        }
        master.state = ReportMasterState::Made;
        master.statistics->width = 999u;
        master.stack = ReportStack{.inputFrames = 30u, .effectiveFrames = 28.5};
        master.lightsCombined = 30;
        for (ReportFrame& frame : master.frames)
        {
            frame.state = ReportFrameState::Used;
            frame.weight = 0.5;
        }
    }
    storeModel(*scenario, earlierRun, stored);

    const CountedBuild fromHistory = buildCounting(*scenario, scenario->runDirectory);

    for (const ReportMaster& master : fromHistory.model.masters)
    {
        if (master.state != ReportMasterState::Reused)
        {
            EXPECT_TRUE(master.madeByRun.isEmpty());
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        EXPECT_EQ(master.madeByRun, earlierRun) << "the figures are marked as another run's";
        EXPECT_EQ(readsOf(fromHistory, master.file), 0);
        ASSERT_TRUE(master.statistics.has_value());
        EXPECT_EQ(master.statistics->width, 999u);
        ASSERT_TRUE(master.stack.has_value());
        EXPECT_DOUBLE_EQ(master.stack->effectiveFrames, 28.5);
        for (const ReportFrame& frame : master.frames)
        {
            EXPECT_EQ(frame.state, ReportFrameState::Used);
            EXPECT_DOUBLE_EQ(frame.weight.value_or(0.0), 0.5);
        }
        EXPECT_FALSE(namesNotStated(fromHistory.model, QStringLiteral("How the master %1").arg(master.name)));
    }
}

TEST(ReportRunFacts, TheLightsOfAMasterThatShareACalibrationAreAGroupOfTheirOwn)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.lightGroups.size(), 5u);
    for (const ReportLightGroup& group : model.lightGroups)
    {
        SCOPED_TRACE(group.name.toStdString());
        const auto master =
            std::find_if(model.masters.begin(), model.masters.end(),
                         [&group](const ReportMaster& candidate) { return candidate.key == group.masterKey; });
        ASSERT_NE(master, model.masters.end());
        EXPECT_EQ(master->lightGroups, QStringList{group.key});
        EXPECT_EQ(group.lights, static_cast<int>(master->frames.size()));
        EXPECT_FALSE(group.preCalibrated);
        EXPECT_FALSE(group.darkMaster.isEmpty());
        EXPECT_TRUE(group.biasMaster.isEmpty()) << "the darks hold their bias";
        const bool narrowband = master->filter == QStringLiteral("Ha");
        EXPECT_EQ(group.flatMaster.isEmpty(), narrowband);
        // Each master the lights take is one the run states, by its anchor.
        const auto statesSlug = [&model](const QString& slug) {
            return std::any_of(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                               [&slug](const ReportCalibrationMaster& candidate) { return candidate.slug == slug; });
        };
        EXPECT_TRUE(statesSlug(group.darkMasterSlug));
        EXPECT_EQ(statesSlug(group.flatMasterSlug), !narrowband);
        const ReportSettingsBlock* calibration = blockOf(group.settings, QStringLiteral("Calibration"));
        ASSERT_NE(calibration, nullptr);
        ASSERT_NE(rowOf(calibration, QStringLiteral("Cosmetic Correction")), nullptr);
        EXPECT_EQ(rowOf(calibration, QStringLiteral("Cosmetic Correction"))->value, QStringLiteral("Yes"));
        EXPECT_NE(rowOf(calibration, QStringLiteral("Hot Pixel Sigma")), nullptr);
        EXPECT_EQ(rowOf(calibration, QStringLiteral("Flat Auto Level")) == nullptr, narrowband)
            << "a setting of a master the lights do not take is not printed";
    }

    // Lights of one master calibrated two ways are two groups of it.
    const QString redKey = masterOfFilter(model, QStringLiteral("Red"))->key;
    const auto red = std::find_if(
        scenario->manifest.integrationGroups.begin(), scenario->manifest.integrationGroups.end(),
        [&redKey](const WorkflowManifest::IntegrationGroup& group) { return group.groupKey.toString() == redKey; });
    ASSERT_NE(red, scenario->manifest.integrationGroups.end());
    QSet<QString> withoutFlat;
    for (std::size_t light = 0; light < 10u; ++light)
    {
        withoutFlat.insert(red->lightFiles[light].canonicalPath());
    }
    for (WorkflowManifest::LightsCalibrationParams& calibration : scenario->manifest.lightsCalibration)
    {
        if (withoutFlat.contains(calibration.lightPath.canonicalPath()))
        {
            calibration.flatPath.reset();
        }
    }
    const ReportModel split = buildScenarioModel(*scenario);
    const ReportMaster* splitRed = masterOfFilter(split, QStringLiteral("Red"));
    ASSERT_EQ(splitRed->lightGroups.size(), 2);
    int lights = 0;
    for (const ReportLightGroup& group : split.lightGroups)
    {
        if (group.masterKey == redKey)
        {
            EXPECT_TRUE(splitRed->lightGroups.contains(group.key));
            EXPECT_TRUE(group.lights == 10 || group.lights == 20);
            EXPECT_EQ(group.flatMaster.isEmpty(), group.lights == 10);
            lights += group.lights;
        }
    }
    EXPECT_EQ(lights, 30);
    QSet<QString> keys;
    for (const ReportLightGroup& group : split.lightGroups)
    {
        keys.insert(group.key);
    }
    EXPECT_EQ(keys.size(), 6) << "each group has a key of its own";
}

// ---- The registration.

TEST(ReportRunFacts, ARegistrationGroupStatesTheReferenceTheApplicationChoseAndWhatItsLightsRecorded)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.registrationGroups.size(), 2u);
    for (std::size_t i = 0; i < model.registrationGroups.size(); ++i)
    {
        const ReportRegistrationGroup& group = model.registrationGroups[i];
        const WorkflowManifest::RegistrationGroup& planned = scenario->manifest.registrationGroups[i];
        ASSERT_FALSE(planned.referenceFrame.has_value()) << "the manifest names no reference";
        // The registration's own record names it: the group's first light.
        EXPECT_EQ(group.reference, QFileInfo(planned.lightFiles.front().canonicalPath()).fileName());
        EXPECT_TRUE(group.referenceChosenAutomatically);
        EXPECT_EQ(group.registered, group.lights);
        EXPECT_EQ(group.failed, 0);
        ASSERT_TRUE(group.overlap.has_value());
        EXPECT_DOUBLE_EQ(*group.overlap, 1.0) << "the masters are the size of the capture";
    }

    const auto failures = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(failures.has_value());
    const ReportRegistrationGroup group = buildScenarioModel(*failures).registrationGroups.front();
    EXPECT_EQ(group.failed, failures->facts.registrationFailures);
    EXPECT_EQ(group.registered.value_or(0) + group.failed.value_or(0), group.lights);
    EXPECT_FALSE(group.medianAstrometricRmsArcsec.has_value()) << "no catalog was matched";

    const auto photometric = built(ReportScenarioId::PhotometricAndLocalWeighting);
    ASSERT_TRUE(photometric.has_value());
    const ReportRegistrationGroup matched = buildScenarioModel(*photometric).registrationGroups.front();
    ASSERT_TRUE(matched.medianAstrometricRmsArcsec.has_value());
    EXPECT_GT(*matched.medianAstrometricRmsArcsec, 0.0);
    ASSERT_TRUE(matched.medianCatalogMatches.has_value());
    EXPECT_NEAR(*matched.medianCatalogMatches, 140.0, 31.0);
}

TEST(ReportRunFacts, AMastersSolutionStatesWhereTheFrameLooks)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    EXPECT_FALSE(buildScenarioModel(*scenario).masters.front().solution.has_value())
        << "a master saved without a solution states none";

    // The master as a plate solve leaves it: a tangent-plane solution at 1.8 arcseconds a pixel.
    using an::tests::fixtures::FixtureCard;
    using an::tests::fixtures::FixtureCardKind;
    const auto real = [](const char* keyword, const char* value) {
        return FixtureCard{keyword, value, {}, FixtureCardKind::Real};
    };
    const std::vector<FixtureCard> cards = {
        textCard("CTYPE1", QStringLiteral("RA---TAN")),
        textCard("CTYPE2", QStringLiteral("DEC--TAN")),
        textCard("RADESYS", QStringLiteral("ICRS")),
        real("CRVAL1", "11.888"),
        real("CRVAL2", "-25.288"),
        real("CRPIX1", "32.5"),
        real("CRPIX2", "24.5"),
        real("CD1_1", "-0.0005"),
        real("CD1_2", "0.0"),
        real("CD2_1", "0.0"),
        real("CD2_2", "0.0005"),
    };
    ASSERT_TRUE(
        writeFloatImage(result.outputPath, 64, 48, an::tests::fixtures::FixtureChannels::Mono, 0.12, {}, cards));
    result.masterSolution = MasterSolutionOutcome::Solved;
    result.masterSolutionDetail = QStringLiteral("Solved by the internal Gaia solver.");

    const ReportMaster master = buildScenarioModel(*scenario).masters.front();

    EXPECT_EQ(master.astrometry, ReportAstrometryOutcome::Solved);
    ASSERT_TRUE(master.solution.has_value());
    EXPECT_NEAR(master.solution->pixelScale, 1.8, 1e-6);
    EXPECT_NEAR(master.solution->fieldWidth, 64.0 * 1.8 / 60.0, 1e-3);
    EXPECT_NEAR(master.solution->fieldHeight, 48.0 * 1.8 / 60.0, 1e-3);
    EXPECT_NEAR(master.solution->centreRightAscension, 11.888, 0.01);
    EXPECT_NEAR(master.solution->centreDeclination, -25.288, 0.01);
}

// ---- The run: its lights as taken, its stages, its resources.

TEST(ReportRunFacts, TheLightsOfEachGroupStateHowTheyWereTaken)
{
    const auto scenario = built(ReportScenarioId::TwoTargets);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    // A row per group, not per filter: two targets taken through the same two filters are four rows.
    ASSERT_EQ(model.run.acquisition.size(), 4u);
    double share = 0.0;
    for (std::size_t i = 0; i < model.run.acquisition.size(); ++i)
    {
        const ReportAcquisitionRow& row = model.run.acquisition[i];
        const ReportMaster& master = model.masters[i];
        EXPECT_EQ(row.groupKey, master.key);
        EXPECT_EQ(row.group, master.name);
        EXPECT_EQ(row.target, master.target);
        EXPECT_EQ(row.filter, master.filter);
        EXPECT_EQ(row.exposure, master.exposure);
        ASSERT_TRUE(row.lowestTemperature.has_value());
        EXPECT_DOUBLE_EQ(*row.lowestTemperature, -10.0);
        EXPECT_DOUBLE_EQ(*row.highestTemperature, -10.0);
        EXPECT_EQ(row.gain, QStringList{QStringLiteral("Gain 100")});
        EXPECT_EQ(row.offset, QStringList{QStringLiteral("Offset 50")});
        share += row.share;
    }
    EXPECT_NEAR(share, 1.0, 1e-9);

    // What the lights' headers state counts the temperature too.
    ASSERT_TRUE(model.run.headers.has_value());
    const auto temperature =
        std::find_if(model.run.headers->fields.begin(), model.run.headers->fields.end(),
                     [](const ReportHeaderField& field) { return field.name == QStringLiteral("Sensor temperature"); });
    ASSERT_NE(temperature, model.run.headers->fields.end());
    EXPECT_EQ(temperature->present, model.run.headers->lights);
    const auto focalLength =
        std::find_if(model.run.headers->fields.begin(), model.run.headers->fields.end(),
                     [](const ReportHeaderField& field) { return field.name == QStringLiteral("Focal length"); });
    ASSERT_NE(focalLength, model.run.headers->fields.end());
    EXPECT_EQ(focalLength->present, 0) << "the scenario's lights state none";
}

TEST(ReportRunFacts, AStageTheRunDidNotTimeSaysWhy)
{
    // A run that stopped in registration never reached the stages after it.
    const auto stopped = built(ReportScenarioId::FailedBeforeAnyMaster);
    ASSERT_TRUE(stopped.has_value());
    const ReportModel stoppedModel = buildScenarioModel(*stopped);
    ASSERT_EQ(stoppedModel.stages.size(), static_cast<std::size_t>(stopped->manifest.stageSequence.size()));
    bool failedSeen = false;
    int notReached = 0;
    for (const ReportStage& stage : stoppedModel.stages)
    {
        if (stage.state == ReportStageState::Failed)
        {
            failedSeen = true;
            continue;
        }
        if (failedSeen)
        {
            EXPECT_EQ(stage.state, ReportStageState::NotReached) << stage.name.toStdString();
            EXPECT_FALSE(stage.durationMs.has_value());
            EXPECT_FALSE(stage.note.isEmpty());
            ++notReached;
        }
    }
    EXPECT_TRUE(failedSeen);
    EXPECT_GT(notReached, 0);
    EXPECT_EQ(stoppedModel.stages.back().key, QStringLiteral("LightsStacking"));

    // An incremental run in which everything was still valid ran no stage: what each makes was reused.
    const auto reused = built(ReportScenarioId::EveryGroupReused);
    ASSERT_TRUE(reused.has_value());
    ASSERT_TRUE(reused->result.stageResults.empty());
    const ReportModel reusedModel = buildScenarioModel(*reused);
    ASSERT_FALSE(reusedModel.stages.empty());
    for (const ReportStage& stage : reusedModel.stages)
    {
        EXPECT_EQ(stage.state, ReportStageState::Reused) << stage.name.toStdString();
        EXPECT_GT(stage.groups, 0);
    }

    // A run that completed timed every stage it lists.
    const auto complete = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(complete.has_value());
    for (const ReportStage& stage : buildScenarioModel(*complete).stages)
    {
        EXPECT_TRUE(stage.durationMs.has_value()) << stage.name.toStdString();
    }
}

TEST(ReportRunFacts, TheEnginesMemoryAndTheCachesCountersAreStatedWhenTheRunCollectedThem)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel without = buildScenarioModel(*scenario);
    EXPECT_FALSE(without.run.engineMemory.has_value());
    EXPECT_TRUE(without.run.cacheDiagnostics.empty());

    scenario->result.engineMemory = WorkflowExecutor::Result::EngineMemory{.peakBytes = 9'000'000'000u,
                                                                           .peakBytesBeforeRun = 1'000'000u,
                                                                           .liveBytesAtEnd = 4096u,
                                                                           .allocations = 1234u,
                                                                           .bytesAllocated = 77'000'000'000u,
                                                                           .deniedAllocations = 2u};
    WorkflowImageCacheDiagnostics cache;
    cache.hits = 41u;
    cache.misses = 7u;
    cache.peakBytes = 123'456'789u;
    scenario->result.workflowCacheDiagnostics = cache;

    const ReportRun run = buildScenarioModel(*scenario).run;

    ASSERT_TRUE(run.engineMemory.has_value());
    EXPECT_EQ(run.engineMemory->peakBytes, 9'000'000'000u);
    EXPECT_EQ(run.engineMemory->deniedAllocations, 2u);
    EXPECT_EQ(run.engineMemory->allocations, 1234u);
    const auto counter = [&run](const QString& key) -> std::optional<std::uint64_t> {
        const auto found = std::find_if(run.cacheDiagnostics.begin(), run.cacheDiagnostics.end(),
                                        [&key](const ReportCounter& candidate) { return candidate.key == key; });
        return found == run.cacheDiagnostics.end() ? std::nullopt : std::optional<std::uint64_t>(found->value);
    };
    EXPECT_EQ(counter(QStringLiteral("hits")), 41u);
    EXPECT_EQ(counter(QStringLiteral("misses")), 7u);
    EXPECT_EQ(counter(QStringLiteral("peakBytes")), 123'456'789u);
    for (const ReportCounter& stated : run.cacheDiagnostics)
    {
        EXPECT_FALSE(stated.name.isEmpty());
    }
}

TEST(ReportRunFacts, WhatTheRunWroteToDiskIsStatedByKind)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    const auto kind = [&model](const QString& name) -> const ReportDiskUse* {
        const auto found = std::find_if(model.run.disk.begin(), model.run.disk.end(),
                                        [&name](const ReportDiskUse& use) { return use.kind == name; });
        return found == model.run.disk.end() ? nullptr : &*found;
    };
    const ReportDiskUse* masters = kind(QStringLiteral("Masters"));
    ASSERT_NE(masters, nullptr);
    EXPECT_EQ(masters->files, scenario->facts.mastersMade);
    qint64 masterBytes = 0;
    for (const IntegrationGroupResult& group : scenario->result.integrationGroupResults)
    {
        masterBytes += group.outputBytes;
    }
    EXPECT_EQ(masters->bytes, masterBytes);

    // The luminance stack wrote its rejection maps and its coverage map.
    const ReportDiskUse* maps = kind(QStringLiteral("Maps"));
    ASSERT_NE(maps, nullptr);
    EXPECT_EQ(maps->files, static_cast<int>(masterOfFilter(model, QStringLiteral("Luminance"))->maps.size()));
    EXPECT_GT(maps->bytes, 0);

    // The bias is an earlier session's: this run wrote the two darks and the four flats.
    const ReportDiskUse* calibration = kind(QStringLiteral("Calibration masters"));
    ASSERT_NE(calibration, nullptr);
    EXPECT_EQ(calibration->files, 6);

    // The registration recorded registered images that are not on disk: a kind with nothing is not listed.
    EXPECT_EQ(kind(QStringLiteral("Registered lights")), nullptr);
}

// ---- What each kind of measurement costs on a real run.

TEST(ReportRealRun, WhatBuildingTheModelOfARealRunCosts)
{
    const QString runDirectory = qEnvironmentVariable("AFTERNIGHT_REPORT_REAL_RUN").trimmed();
    if (runDirectory.isEmpty())
    {
        GTEST_SKIP() << "set AFTERNIGHT_REPORT_REAL_RUN to a run's folder under history/ to time its report model";
    }
    QFile manifestFile(QDir(runDirectory).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly)) << manifestFile.fileName().toStdString();
    const QJsonObject archiveRoot = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const std::optional<ArchivedWorkflowRun> run = readArchivedWorkflowRun(runDirectory, archiveRoot);
    ASSERT_TRUE(run.has_value());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());

    /// Times each read of a source through another.
    struct TimedImages final : IReportImages
    {
        explicit TimedImages(IReportImages* innerIn) : inner(innerIn) {}
        std::optional<ReportImageMeasurement> measure(const QString& path, const std::uint32_t width) override
        {
            QElapsedTimer timer;
            timer.start();
            std::optional<ReportImageMeasurement> measured = inner->measure(path, width);
            times.append({path, timer.elapsed(), QFileInfo(path).size()});
            return measured;
        }
        std::optional<ReportImageLevels> measureLevels(const QString& path, const std::uint32_t width) override
        {
            QElapsedTimer timer;
            timer.start();
            std::optional<ReportImageLevels> measured = inner->measureLevels(path, width);
            times.append({path, timer.elapsed(), QFileInfo(path).size()});
            return measured;
        }
        struct Read
        {
            QString path;
            qint64 ms;
            qint64 bytes;
        };
        IReportImages* inner;
        QList<Read> times;
    };
    struct TimedHeaders final : IReportHeaders
    {
        explicit TimedHeaders(IReportHeaders* innerIn) : inner(innerIn) {}
        std::optional<ReportHeader> probe(const QString& path) override
        {
            QElapsedTimer timer;
            timer.start();
            std::optional<ReportHeader> header = inner->probe(path);
            totalMs += timer.elapsed();
            ++probes;
            return header;
        }
        IReportHeaders* inner;
        qint64 totalMs = 0;
        int probes = 0;
    };

    const ProductionReportSources production(&*session,
                                             QDir(runDirectory).filePath(QStringLiteral("registration_metrics.db")));
    ReportSources sources = production.sources();
    TimedImages images(sources.images);
    TimedHeaders headers(sources.headers);
    sources.images = &images;
    sources.headers = &headers;

    QElapsedTimer total;
    total.start();
    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = run->manifest,
                                                                 .result = run->result,
                                                                 .runDirectory = runDirectory,
                                                                 .context = archiveRoot,
                                                                 .counters = std::nullopt},
                                               sources);
    const qint64 totalMs = total.elapsed();

    QSet<QString> calibrationFiles;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        calibrationFiles.insert(QDir::cleanPath(QFileInfo(master.file).absoluteFilePath()));
    }
    qint64 masterMs = 0;
    qint64 calibrationMs = 0;
    qint64 masterBytes = 0;
    qint64 calibrationBytes = 0;
    int masterReads = 0;
    int calibrationReads = 0;
    for (const TimedImages::Read& read : images.times)
    {
        const bool calibration = calibrationFiles.contains(read.path);
        (calibration ? calibrationMs : masterMs) += read.ms;
        (calibration ? calibrationBytes : masterBytes) += read.bytes;
        (calibration ? calibrationReads : masterReads) += 1;
    }
    int lights = 0;
    for (const ReportMaster& master : model.masters)
    {
        lights += static_cast<int>(master.frames.size());
    }
    const QString storedJson =
        QString::fromUtf8(QJsonDocument(ReportModelJson::toJson(model)).toJson(QJsonDocument::Compact));
    std::cout << "[report-cost] run " << QFileInfo(runDirectory).fileName().toStdString() << ": " << lights
              << " lights, " << model.masters.size() << " masters, " << model.calibrationMasters.size()
              << " calibration masters\n"
              << "[report-cost] the whole model: " << totalMs
              << " ms; today's report phase: " << run->result.executorTiming.reportMs << " ms\n"
              << "[report-cost] masters measured: " << masterReads << " reads, " << masterMs << " ms, "
              << masterBytes / (1024 * 1024) << " MiB\n"
              << "[report-cost] calibration masters measured: " << calibrationReads << " reads, " << calibrationMs
              << " ms, " << calibrationBytes / (1024 * 1024) << " MiB\n"
              << "[report-cost] headers probed: " << headers.probes << " probes, " << headers.totalMs << " ms\n"
              << "[report-cost] everything else: " << totalMs - masterMs - calibrationMs - headers.totalMs << " ms\n"
              << "[report-cost] the stored model: " << storedJson.size() / 1024 << " KiB; issues "
              << model.issues.size() << ", not stated " << model.notStated.size() << std::endl;
    for (const ReportNotStated& entry : model.notStated)
    {
        std::cout << "[report-cost] not stated: " << entry.what.toStdString() << " - " << entry.why.toStdString()
                  << '\n';
    }
    EXPECT_GT(lights, 0);
}

// What the pictures a run's archive keeps cost on a real run: each master's whole frame and detail from one read of
// its file, and each calibration master's picture, as the archiver makes them when the run prepared none. Skipped
// unless AFTERNIGHT_REPORT_REAL_RUN names a run's folder; the run's files are only read, and the pictures are written
// under a temporary directory.
TEST(ReportRealRun, WhatThePicturesOfARealRunCost)
{
    const QString runDirectory = qEnvironmentVariable("AFTERNIGHT_REPORT_REAL_RUN").trimmed();
    if (runDirectory.isEmpty())
    {
        GTEST_SKIP() << "set AFTERNIGHT_REPORT_REAL_RUN to a run's folder under history/ to time its pictures";
    }
    QFile manifestFile(QDir(runDirectory).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly)) << manifestFile.fileName().toStdString();
    const QJsonObject archiveRoot = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const std::optional<ArchivedWorkflowRun> run = readArchivedWorkflowRun(runDirectory, archiveRoot);
    ASSERT_TRUE(run.has_value());
    std::optional<an::consumer::EngineSession> session = openReportSession();
    ASSERT_TRUE(session.has_value());
    QTemporaryDir pictures;
    ASSERT_TRUE(pictures.isValid());
    const QSize bound(2048, 2048);

    int masters = 0;
    int details = 0;
    qint64 masterMs = 0;
    qint64 masterBytes = 0;
    for (const IntegrationGroupResult& group : run->result.integrationGroupResults)
    {
        // An archive names the master it kept relative to its own folder.
        const QString path = QDir(runDirectory).filePath(group.outputPath);
        if (!group.success || !QFileInfo::exists(path))
        {
            continue;
        }
        QElapsedTimer timer;
        timer.start();
        const ImageIntegration::ReportMasterPreviews written = ImageIntegration::generateReportMasterPreviews(
            *session, path, QDir(pictures.path()).filePath(QStringLiteral("master_%1").arg(masters)),
            QStringLiteral("master.jpg"), QStringLiteral("detail.jpg"), bound);
        masterMs += timer.elapsed();
        masterBytes += QFileInfo(path).size();
        EXPECT_TRUE(written.whole.has_value()) << path.toStdString();
        details += written.detail.has_value() ? 1 : 0;
        ++masters;
    }

    int calibrationMasters = 0;
    qint64 calibrationMs = 0;
    qint64 calibrationBytes = 0;
    const QString calibrationDir = QDir(pictures.path()).filePath(QStringLiteral("calibration"));
    for (const ImageIntegration::ArchivedCalibrationPreview& preview :
         ImageIntegration::archivedCalibrationPreviews(run->manifest))
    {
        if (preview.masterPath.isEmpty() || !QFileInfo::exists(preview.masterPath))
        {
            continue;
        }
        QElapsedTimer timer;
        timer.start();
        // The archiver bounds a calibration master's picture at half a master's.
        const std::optional<QString> written = ImageIntegration::generateReportPreviewJpeg(
            *session, preview.masterPath, calibrationDir, preview.fileName, bound / 2);
        calibrationMs += timer.elapsed();
        calibrationBytes += QFileInfo(preview.masterPath).size();
        EXPECT_TRUE(written.has_value()) << preview.masterPath.toStdString();
        ++calibrationMasters;
    }

    qint64 masterPictureBytes = 0;
    qint64 calibrationPictureBytes = 0;
    QDirIterator written(pictures.path(), QDir::Files, QDirIterator::Subdirectories);
    while (written.hasNext())
    {
        written.next();
        (written.filePath().startsWith(calibrationDir) ? calibrationPictureBytes : masterPictureBytes) +=
            written.fileInfo().size();
    }
    std::cout << "[picture-cost] run " << QFileInfo(runDirectory).fileName().toStdString() << "\n"
              << "[picture-cost] masters: " << masters << " read (" << masterBytes / (1024 * 1024) << " MiB), "
              << details << " with a detail, " << masterMs << " ms, pictures " << masterPictureBytes / 1024 << " KiB\n"
              << "[picture-cost] calibration masters: " << calibrationMasters << " read ("
              << calibrationBytes / (1024 * 1024) << " MiB), " << calibrationMs << " ms, pictures "
              << calibrationPictureBytes / 1024 << " KiB" << std::endl;
}
