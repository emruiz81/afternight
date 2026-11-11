// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runs the report's suites build: each scenario holds what it says it holds, in the structures and files a real
/// run leaves behind, so a suite that asserts on a report of one asserts on something a run can produce.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportScenarios.h"

#include "processes/ImageIntegration/reporting/WorkflowReportBuilder.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

using namespace an::tests::integration::report;

namespace
{
/// A scenario's name, for a failure message.
std::string nameOf(ReportScenarioId id)
{
    return reportScenarioName(id).toStdString();
}

/// The root of an archived run's manifest document; empty when the file is missing or is not a document.
QJsonObject archivedManifestRoot(const QString& runDirectory)
{
    QFile file(QDir(runDirectory).filePath(QStringLiteral("workflow_manifest.json")));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// The lights of the manifest's integration group that ends in `|<filter>|<target>|-`; empty when it has none.
std::vector<FrameId> lightsOf(const WorkflowManifest& manifest, const QString& filter)
{
    for (const WorkflowManifest::IntegrationGroup& group : manifest.integrationGroups)
    {
        if (group.filter == filter)
        {
            return group.lightFiles;
        }
    }
    return {};
}

/// How many lights the manifest's integration groups hold.
int lightsInGroups(const WorkflowManifest& manifest)
{
    int lights = 0;
    for (const WorkflowManifest::IntegrationGroup& group : manifest.integrationGroups)
    {
        lights += static_cast<int>(group.lightFiles.size());
    }
    return lights;
}

/// The result of the group whose key holds `|<filter>|`, or nothing.
const IntegrationGroupResult* resultOf(const WorkflowExecutor::Result& result, const QString& filter)
{
    const QString part = QStringLiteral("|%1|").arg(filter);
    const auto found =
        std::find_if(result.integrationGroupResults.begin(), result.integrationGroupResults.end(),
                     [&part](const IntegrationGroupResult& group) {
                         return group.groupKey.contains(part) && !group.groupKey.startsWith(QStringLiteral("rgb"));
                     });
    return found == result.integrationGroupResults.end() ? nullptr : &*found;
}
} // namespace

TEST(ReportScenarios, EveryScenarioHasANameOfItsOwn)
{
    QSet<QString> names;
    for (const ReportScenarioId id : allReportScenarios())
    {
        const QString name = reportScenarioName(id);
        EXPECT_FALSE(name.isEmpty());
        EXPECT_NE(name, QStringLiteral("unknown"));
        EXPECT_FALSE(names.contains(name)) << name.toStdString() << " names two scenarios";
        names.insert(name);
    }
    EXPECT_EQ(names.size(), 23);
}

TEST(ReportScenarios, EveryScenarioHoldsWhatItSaysItHolds)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(nameOf(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportScenarioFacts& facts = scenario->facts;

        EXPECT_EQ(scenario->name, reportScenarioName(id));
        EXPECT_TRUE(QFileInfo(scenario->outputDirectory).isDir());
        EXPECT_EQ(scenario->manifest.outputDirectory, scenario->outputDirectory);
        EXPECT_FALSE(scenario->manifest.executionId.isEmpty());
        EXPECT_TRUE(scenario->runDirectory.endsWith(scenario->manifest.executionId));
        EXPECT_FALSE(QFileInfo::exists(scenario->runDirectory)) << "a scenario's own run is not archived yet";

        // Every planned master met exactly one fate.
        EXPECT_EQ(facts.mastersPlanned,
                  facts.mastersMade + facts.mastersFailed + facts.mastersReused + facts.mastersNotReached);

        // The results say the same as the facts, and every master made is a file of the size its result states.
        int made = 0;
        int failed = 0;
        for (const IntegrationGroupResult& group : scenario->result.integrationGroupResults)
        {
            if (!group.success)
            {
                ++failed;
                EXPECT_TRUE(group.outputPath.isEmpty());
                continue;
            }
            ++made;
            EXPECT_TRUE(QFileInfo::exists(group.outputPath)) << group.outputPath.toStdString();
            EXPECT_GT(group.outputBytes, 0);
            EXPECT_EQ(QFileInfo(group.outputPath).size(), group.outputBytes);

            if (!group.qualityReport.valid)
            {
                continue;
            }
            // The stack's record names the lights it combined, each with its weight and - where the stack
            // rejects - its rejected fraction, in one order.
            const StackQualityRecord& quality = group.qualityReport;
            EXPECT_GT(group.lightFrameCount, 0);
            EXPECT_EQ(quality.inputFrameCount, static_cast<std::uint64_t>(group.lightFrameCount));
            EXPECT_EQ(quality.lightWeights.size(), static_cast<std::size_t>(group.lightFrameCount));
            if (quality.hasRejectionStats)
            {
                EXPECT_EQ(quality.frameRejectionRates.size(), quality.lightWeights.size());
            }
            EXPECT_EQ(quality.spatialRestackExcludedFrameIndices.size(),
                      quality.spatialRestackExcludedFrameRates.size());
        }
        EXPECT_EQ(made, facts.mastersMade);
        EXPECT_EQ(failed, facts.mastersFailed);
        EXPECT_EQ(scenario->madeMasterPaths().size(), facts.mastersMade);

        // The manifest holds every light taken but the ones the user left out of a stack or of the run.
        EXPECT_EQ(lightsInGroups(scenario->manifest) + facts.lightsLeftOutByUser, facts.lightsTaken);
        EXPECT_EQ(scenario->lightsLeftOutByUser.size(), static_cast<std::size_t>(facts.lightsLeftOutByUser));
        EXPECT_EQ(scenario->manifest.registrationGroups.size(), static_cast<std::size_t>(facts.registrationGroups));
        for (const WorkflowManifest::RegistrationGroup& group : scenario->manifest.registrationGroups)
        {
            EXPECT_EQ(group.lightExposureSeconds.size(), group.lightFiles.size());
            EXPECT_EQ(group.lightCaptureTimestampsUtc.size(), group.lightFiles.size());
        }
        for (const WorkflowManifest::IntegrationGroup& group : scenario->manifest.integrationGroups)
        {
            EXPECT_EQ(group.lightExposureSeconds.size(), group.lightFiles.size());
            EXPECT_FALSE(group.lightFiles.empty());
        }

        // The manifest is one the product reads back as it wrote it.
        const QJsonObject written = scenario->manifest.toJson();
        EXPECT_EQ(WorkflowManifest::fromJson(written).toJson(), written);

        EXPECT_EQ(scenario->session() == nullptr, id == ReportScenarioId::FiveFiltersWithoutSession);
        EXPECT_EQ(scenario->metricsDatabasePath.isEmpty(), id == ReportScenarioId::FiveFiltersWithoutMetrics);
        if (!scenario->metricsDatabasePath.isEmpty())
        {
            EXPECT_TRUE(QFileInfo::exists(scenario->metricsDatabasePath));
        }
        EXPECT_EQ(scenario->result.warningCount, scenario->result.warnings.size());
        EXPECT_FALSE(scenario->context.value(QStringLiteral("timeZoneId")).toString().isEmpty());
    }
}

TEST(ReportScenarios, ARunsFilesAreGoneWithItsLastCopy)
{
    QString outputDirectory;
    {
        std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::OneMonoMaster);
        ASSERT_TRUE(scenario.has_value());
        outputDirectory = scenario->outputDirectory;
        const ReportScenario copy = *scenario;
        scenario.reset();
        EXPECT_TRUE(QFileInfo::exists(outputDirectory)) << "a copy keeps the run's files";
        EXPECT_TRUE(QFileInfo::exists(copy.madeMasterPaths().value(0)));
    }
    EXPECT_FALSE(QFileInfo::exists(outputDirectory));
}

TEST(ReportScenarios, TheFiveFiltersHoldSixMastersOf174LightsInTwoRegistrationGroups)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const ReportScenarioFacts& facts = scenario->facts;

    EXPECT_EQ(facts.lightsTaken, 174);
    EXPECT_EQ(facts.lightsCombined, 174);
    EXPECT_EQ(facts.mastersPlanned, 6);
    EXPECT_EQ(facts.mastersMade, 6);
    EXPECT_EQ(facts.registrationGroups, 2);
    EXPECT_EQ(facts.nights, 2);
    EXPECT_EQ(lightsOf(scenario->manifest, QStringLiteral("Luminance")).size(), 60u);
    EXPECT_EQ(lightsOf(scenario->manifest, QStringLiteral("Red")).size(), 30u);
    EXPECT_EQ(lightsOf(scenario->manifest, QStringLiteral("Green")).size(), 30u);
    EXPECT_EQ(lightsOf(scenario->manifest, QStringLiteral("Blue")).size(), 30u);
    EXPECT_EQ(lightsOf(scenario->manifest, QStringLiteral("Ha")).size(), 24u);

    // The combined RGB output is a result with no group of its own, counting its three channels' lights.
    EXPECT_EQ(scenario->manifest.integrationGroups.size(), 5u);
    ASSERT_EQ(scenario->result.integrationGroupResults.size(), 6u);
    const IntegrationGroupResult& rgb = scenario->result.integrationGroupResults.back();
    EXPECT_TRUE(rgb.groupKey.startsWith(QStringLiteral("rgb|")));
    EXPECT_EQ(rgb.lightFrameCount, 90);
    EXPECT_TRUE(scenario->manifest.combineRgbImages);

    // One bias left by an earlier session, two darks, a flat per broadband filter and none for hydrogen-alpha.
    EXPECT_EQ(facts.calibrationMasters, 7);
    ASSERT_TRUE(scenario->manifest.biasStage.has_value());
    ASSERT_EQ(scenario->manifest.biasStage->groups.size(), 1u);
    EXPECT_TRUE(scenario->manifest.biasStage->groups.front().skipIfExists);
    EXPECT_FALSE(scenario->manifest.biasStage->groups.front().existingMaster.has_value());
    EXPECT_EQ(scenario->result.calibrationGroupResults.size(), 6u) << "a reused master has no result";
    int hydrogenAlphaEntries = 0;
    for (const WorkflowManifest::LightsCalibrationParams& entry : scenario->manifest.lightsCalibration)
    {
        if (entry.lightPath.canonicalPath().contains(QStringLiteral("_Ha_")))
        {
            ++hydrogenAlphaEntries;
            EXPECT_TRUE(entry.darkPath.has_value());
            EXPECT_FALSE(entry.flatPath.has_value());
        }
        else
        {
            EXPECT_TRUE(entry.flatPath.has_value());
        }
    }
    EXPECT_EQ(hydrogenAlphaEntries, 24);
    EXPECT_EQ(scenario->manifest.lightsCalibration.size(), 174u);

    // The luminance stack wrote its maps beside its master.
    const IntegrationGroupResult* luminance = resultOf(scenario->result, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_EQ(luminance->rejectionMaps.size(), 2u);
    ASSERT_EQ(luminance->qualityMaps.size(), 1u);
    EXPECT_TRUE(QFileInfo::exists(luminance->rejectionMaps.front().path));
    EXPECT_TRUE(QFileInfo::exists(luminance->qualityMaps.front().path));

    // Every light is a file whose header a report can read.
    for (const WorkflowManifest::IntegrationGroup& group : scenario->manifest.integrationGroups)
    {
        for (const FrameId& light : group.lightFiles)
        {
            ASSERT_TRUE(QFileInfo::exists(light.canonicalPath())) << light.canonicalPath().toStdString();
        }
    }
    EXPECT_TRUE(QFileInfo::exists(
        QDir(scenario->outputDirectory).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"))));
}

TEST(ReportScenarios, TheStoredMetricsAreTheOnesARegistrationWrites)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_FALSE(scenario->metricsDatabasePath.isEmpty());

    const QString connection =
        QStringLiteral("ReportScenariosTest_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(scenario->metricsDatabasePath);
        ASSERT_TRUE(database.open());

        // A database a registration made states its format; the bare table the older cases write states none.
        QSqlQuery format(database);
        ASSERT_TRUE(format.exec(QStringLiteral("PRAGMA user_version")));
        ASSERT_TRUE(format.next());
        EXPECT_GT(format.value(0).toInt(), 0);

        QSqlQuery state(database);
        ASSERT_TRUE(state.exec(QStringLiteral("SELECT group_key, reference_frame_id FROM registration_group_state")));
        ASSERT_TRUE(state.next());
        EXPECT_EQ(state.value(0).toString(), scenario->manifest.registrationGroups.front().groupKey.toString());
        EXPECT_EQ(state.value(1).toString(),
                  scenario->manifest.registrationGroups.front().lightFiles.front().canonicalPath());

        QSqlQuery rows(database);
        ASSERT_TRUE(
            rows.exec(QStringLiteral("SELECT succeeded, fwhm, stars, background_noise, transform_json, output_path "
                                     "FROM registration_metrics ORDER BY updated_at")));
        int lights = 0;
        int moved = 0;
        while (rows.next())
        {
            ++lights;
            EXPECT_EQ(rows.value(0).toInt(), 1);
            EXPECT_GT(rows.value(1).toDouble(), 1.0);
            EXPECT_GT(rows.value(2).toInt(), 100);
            EXPECT_GT(rows.value(3).toDouble(), 0.0);
            EXPECT_FALSE(rows.value(5).toString().isEmpty());

            an::RegistrationTransform transform;
            QString error;
            ASSERT_TRUE(stage_utils::parseRegistrationTransformJson(rows.value(4).toString(), transform, error))
                << error.toStdString();
            const an::RegistrationTransformRequest request = transform.toRequest();
            if (request.sourceToReference[2] != 0.0 || request.sourceToReference[5] != 0.0)
            {
                ++moved;
            }
        }
        EXPECT_EQ(lights, 18);
        EXPECT_EQ(moved, 17) << "every light but the reference is shifted";
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

TEST(ReportScenarios, EveryWayALightIsLeftOutIsCounted)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    const ReportScenarioFacts& facts = scenario->facts;

    EXPECT_EQ(facts.lightsTaken, 46);
    EXPECT_EQ(facts.lightsLeftOutByUser, 11);
    EXPECT_EQ(facts.registrationFailures, 4);
    EXPECT_EQ(facts.lightsDroppedByStack, 1);
    EXPECT_EQ(facts.lightsExcludedOnRestack, 2);
    EXPECT_EQ(facts.lightsCombined, 28);
    EXPECT_EQ(facts.groupsLeftOutWhole, 1);
    // Every light met one fate.
    EXPECT_EQ(facts.lightsTaken, facts.lightsLeftOutByUser + facts.registrationFailures + facts.lightsDroppedByStack +
                                     facts.lightsExcludedOnRestack + facts.lightsCombined);

    // A group whose every light is left out is no group; a light left out of a stack is still registered.
    ASSERT_EQ(scenario->manifest.integrationGroups.size(), 1u);
    const WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    EXPECT_EQ(group.lightFiles.size(), 35u);
    ASSERT_EQ(scenario->manifest.registrationGroups.size(), 1u);
    EXPECT_EQ(scenario->manifest.registrationGroups.front().lightFiles.size(), 37u);

    int fromStackOnly = 0;
    int wholeGroup = 0;
    QSet<QString> inGroup;
    for (const FrameId& light : group.lightFiles)
    {
        inGroup.insert(light.canonicalPath());
    }
    for (const ReportScenarioLeftOutLight& light : scenario->lightsLeftOutByUser)
    {
        EXPECT_FALSE(inGroup.contains(FrameId(light.path).canonicalPath()));
        fromStackOnly += light.fromStackOnly ? 1 : 0;
        wholeGroup += light.integrationGroupKey.contains(QStringLiteral("|Oiii|")) ? 1 : 0;
    }
    EXPECT_EQ(fromStackOnly, 2);
    EXPECT_EQ(wholeGroup, 6);

    // The stack's record: the lights it kept, and the two it flagged by their place in the group's list.
    ASSERT_EQ(scenario->result.integrationGroupResults.size(), 1u);
    const StackQualityRecord& quality = scenario->result.integrationGroupResults.front().qualityReport;
    EXPECT_TRUE(quality.spatialRestackApplied);
    EXPECT_EQ(quality.lightWeights.size(), 28u);
    ASSERT_EQ(quality.spatialRestackExcludedFrameIndices.size(), 2u);
    QSet<QString> kept;
    for (const StackLightWeight& weight : quality.lightWeights)
    {
        kept.insert(weight.light);
    }
    for (const std::uint64_t index : quality.spatialRestackExcludedFrameIndices)
    {
        ASSERT_LT(index, group.lightFiles.size());
        EXPECT_FALSE(kept.contains(group.lightFiles[static_cast<std::size_t>(index)].canonicalPath()));
    }

    // The dropped light is named in the stage's warning and, with its reason, on the group's result.
    EXPECT_EQ(scenario->result.warnings.filter(QStringLiteral("Skipping 1 light frame(s): ")).size(), 1);
    ASSERT_EQ(scenario->result.integrationGroupResults.front().droppedLights.size(), 1u);
    EXPECT_EQ(scenario->result.integrationGroupResults.front().droppedLights.front().reason,
              DroppedLightReason::NoRegisteredOutput);
    EXPECT_FALSE(kept.contains(scenario->result.integrationGroupResults.front().droppedLights.front().light));

    // The manifest names each light the user left out, with its exclusion and the group it would have joined.
    ASSERT_EQ(scenario->manifest.leftOutLights.size(), scenario->lightsLeftOutByUser.size());
    for (std::size_t i = 0; i < scenario->lightsLeftOutByUser.size(); ++i)
    {
        const ReportScenarioLeftOutLight& light = scenario->lightsLeftOutByUser[i];
        const WorkflowManifest::LeftOutLight& named = scenario->manifest.leftOutLights[i];
        EXPECT_EQ(named.light.canonicalPath(), FrameId(light.path).canonicalPath());
        EXPECT_EQ(named.groupKey.toString(), light.integrationGroupKey);
        EXPECT_EQ(named.kind == WorkflowManifest::LeftOutLight::Kind::StackExcluded, light.fromStackOnly);
    }
}

TEST(ReportScenarios, AMasterARunReusedIsNamedInTheRunsRecordAndHasNoResult)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(reportScenarioName(id).toStdString());
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        EXPECT_EQ(scenario->result.reusedIntegrationGroups.size(),
                  static_cast<std::size_t>(scenario->facts.mastersReused));
        for (const ReusedIntegrationGroup& reused : scenario->result.reusedIntegrationGroups)
        {
            EXPECT_TRUE(QFileInfo::exists(reused.outputPath)) << reused.outputPath.toStdString();
            EXPECT_TRUE(std::none_of(
                scenario->result.integrationGroupResults.begin(), scenario->result.integrationGroupResults.end(),
                [&reused](const IntegrationGroupResult& group) { return group.groupKey == reused.groupKey; }));
        }
    }
}

TEST(ReportScenarios, ARunThatStoppedKeepsTheMastersItMade)
{
    const std::optional<ReportScenario> failed = buildReportScenario(ReportScenarioId::FailedInStacking);
    ASSERT_TRUE(failed.has_value());
    EXPECT_FALSE(failed->result.success);
    EXPECT_FALSE(failed->result.cancelled);
    EXPECT_TRUE(failed->result.partialSuccess);
    EXPECT_EQ(failed->facts.mastersMade, 3);
    EXPECT_EQ(failed->facts.mastersFailed, 1);
    EXPECT_EQ(failed->facts.mastersNotReached, 1);
    EXPECT_TRUE(failed->archivedByExecutor);
    ASSERT_FALSE(failed->result.stageResults.empty());
    EXPECT_EQ(failed->result.stageResults.back().stageName, QStringLiteral("LightsStacking"));
    EXPECT_EQ(failed->result.stageResults.back().status, QStringLiteral("failed"));
    EXPECT_EQ(failed->result.errors.size(), 1);
    const IntegrationGroupResult* blue = resultOf(failed->result, QStringLiteral("Blue"));
    ASSERT_NE(blue, nullptr);
    EXPECT_FALSE(blue->success);
    ASSERT_FALSE(blue->finalizationTiming.phases.empty());
    EXPECT_EQ(blue->finalizationTiming.phases.back().status, QStringLiteral("failed"));
    EXPECT_EQ(resultOf(failed->result, QStringLiteral("Ha")), nullptr) << "a group never reached has no result";

    const std::optional<ReportScenario> cancelled = buildReportScenario(ReportScenarioId::CancelledAfterTwoMasters);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_FALSE(cancelled->result.success);
    EXPECT_TRUE(cancelled->result.cancelled);
    EXPECT_EQ(cancelled->facts.mastersMade, 2);
    EXPECT_EQ(cancelled->facts.mastersNotReached, 3);
    EXPECT_TRUE(cancelled->archivedByExecutor);
    EXPECT_EQ(cancelled->result.stageResults.back().status, QStringLiteral("cancelled"));
}

TEST(ReportScenarios, ARunThatStoppedBeforeAnyMasterIsOneTheExecutorDoesNotArchive)
{
    for (const ReportScenarioId id :
         {ReportScenarioId::CancelledBeforeAnyMaster, ReportScenarioId::FailedBeforeAnyMaster})
    {
        SCOPED_TRACE(nameOf(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        EXPECT_FALSE(scenario->archivedByExecutor);
        EXPECT_FALSE(scenario->result.success);
        EXPECT_FALSE(scenario->result.partialSuccess);
        EXPECT_EQ(scenario->result.cancelled, id == ReportScenarioId::CancelledBeforeAnyMaster);
        EXPECT_EQ(scenario->facts.mastersMade, 0);
        EXPECT_TRUE(scenario->result.integrationGroupResults.empty());
        EXPECT_EQ(scenario->facts.mastersNotReached, 5);
        ASSERT_FALSE(scenario->result.stageResults.empty());
        EXPECT_EQ(scenario->result.stageResults.back().stageName, QStringLiteral("LightsRegistration"));
    }
}

TEST(ReportScenarios, AnEarlierRunIsInTheHistoryBeforeTheScenariosOwn)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const QString earlier = scenario->earlierRunDirectories.front();
    EXPECT_NE(QDir::cleanPath(earlier), QDir::cleanPath(scenario->runDirectory));
    EXPECT_EQ(QFileInfo(earlier).dir().dirName(), QStringLiteral("history"));

    // The earlier run is an archive as the product writes one: its manifest, its report, its log, its previews and
    // the metrics its registration had stored.
    const QJsonObject root = archivedManifestRoot(earlier);
    ASSERT_FALSE(root.isEmpty());
    EXPECT_EQ(root.value(QStringLiteral("overallResult")).toString(), QStringLiteral("success"));
    EXPECT_GT(root.value(QStringLiteral("schemaVersion")).toInt(), 0);
    EXPECT_EQ(root.value(QStringLiteral("integrationGroupResults")).toArray().size(), 6);
    EXPECT_TRUE(QFileInfo::exists(QDir(earlier).filePath(QStringLiteral("workflow_report.html"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(earlier).filePath(QStringLiteral("workflow_log.txt"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(earlier).filePath(QStringLiteral("registration_metrics.db"))));
    EXPECT_FALSE(QDir(QDir(earlier).filePath(QStringLiteral("report_previews"))).isEmpty());

    // It stacked the first night alone: 87 lights where the scenario's run takes 174.
    const WorkflowManifest earlierManifest =
        WorkflowManifest::fromJson(root.value(QStringLiteral("manifest")).toObject());
    EXPECT_EQ(lightsInGroups(earlierManifest), 87);
    EXPECT_EQ(scenario->facts.lightsTaken, 174);

    // The lights of the first night are the same files in both runs.
    const std::vector<FrameId> before = lightsOf(earlierManifest, QStringLiteral("Red"));
    const std::vector<FrameId> after = lightsOf(scenario->manifest, QStringLiteral("Red"));
    ASSERT_EQ(before.size(), 14u);
    ASSERT_EQ(after.size(), 30u);
    for (std::size_t light = 0; light < before.size(); ++light)
    {
        EXPECT_EQ(before[light], after[light]);
    }
}

TEST(ReportScenarios, AnIncrementalRunHasNoResultForAMasterItReused)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::IncrementalRun);
    ASSERT_TRUE(scenario.has_value());
    EXPECT_EQ(scenario->manifest.executionMode, ExecutionMode::Incremental);
    EXPECT_TRUE(scenario->result.success);
    EXPECT_EQ(scenario->facts.mastersPlanned, 5);
    EXPECT_EQ(scenario->facts.mastersMade, 2);
    EXPECT_EQ(scenario->facts.mastersReused, 3);
    EXPECT_EQ(scenario->result.integrationGroupResults.size(), 2u);
    EXPECT_EQ(scenario->manifest.incrementalIntegrationGroups.size(), 2u);
    EXPECT_TRUE(scenario->result.calibrationGroupResults.empty()) << "no calibration master was remade";

    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const WorkflowManifest earlier = WorkflowManifest::fromJson(
        archivedManifestRoot(scenario->earlierRunDirectories.front()).value(QStringLiteral("manifest")).toObject());

    for (const QString& filter : {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")})
    {
        SCOPED_TRACE(filter.toStdString());
        // Reused: no result, the master the earlier run made still in the folder, and the very lights it stacked.
        EXPECT_EQ(resultOf(scenario->result, filter), nullptr);
        const auto group = std::find_if(
            scenario->manifest.integrationGroups.begin(), scenario->manifest.integrationGroups.end(),
            [&filter](const WorkflowManifest::IntegrationGroup& candidate) { return candidate.filter == filter; });
        ASSERT_NE(group, scenario->manifest.integrationGroups.end());
        EXPECT_TRUE(group->skipIfExists);
        EXPECT_TRUE(QFileInfo::exists(QDir(scenario->outputDirectory).filePath(group->outputFilename)));
        EXPECT_EQ(group->lightFiles, lightsOf(earlier, filter));
    }
    for (const QString& filter : {QStringLiteral("Luminance"), QStringLiteral("Ha")})
    {
        SCOPED_TRACE(filter.toStdString());
        // Regenerated: a result, and more lights than the earlier run stacked.
        const IntegrationGroupResult* made = resultOf(scenario->result, filter);
        ASSERT_NE(made, nullptr);
        EXPECT_TRUE(made->success);
        EXPECT_GT(lightsOf(scenario->manifest, filter).size(), lightsOf(earlier, filter).size());
    }

    const std::optional<ReportScenario> allReused = buildReportScenario(ReportScenarioId::EveryGroupReused);
    ASSERT_TRUE(allReused.has_value());
    EXPECT_TRUE(allReused->result.success);
    EXPECT_TRUE(allReused->result.integrationGroupResults.empty());
    EXPECT_TRUE(allReused->result.stageResults.empty());
    EXPECT_EQ(allReused->facts.mastersReused, 5);
    EXPECT_EQ(allReused->facts.mastersMade, 0);
}

TEST(ReportScenarios, AScenariosRunIsArchivedAsTheExecutorArchivesARun)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    const QDir run(scenario->runDirectory);
    const QJsonObject root = archivedManifestRoot(scenario->runDirectory);
    ASSERT_FALSE(root.isEmpty());
    EXPECT_EQ(root.value(QStringLiteral("overallResult")).toString(), QStringLiteral("success"));
    EXPECT_TRUE(run.exists(QStringLiteral("workflow_report.html")));
    EXPECT_TRUE(run.exists(QStringLiteral("registration_metrics.db")));

    // Every master was kept in the history and has the preview the report shows.
    for (const IntegrationGroupResult& group : scenario->result.integrationGroupResults)
    {
        SCOPED_TRACE(group.groupKey.toStdString());
        EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::RetainedInHistory);
    }
    QStringList previews =
        QDir(run.filePath(QStringLiteral("report_previews"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    // The calibration masters' pictures have a folder of their own beside the masters'.
    EXPECT_TRUE(previews.removeOne(QStringLiteral("calibration")));
    EXPECT_EQ(previews.size(), 6);
    for (const QString& group : previews)
    {
        EXPECT_TRUE(run.exists(QStringLiteral("report_previews/%1/master.jpg").arg(group))) << group.toStdString();
        EXPECT_TRUE(run.exists(QStringLiteral("report_previews/%1/detail.jpg").arg(group))) << group.toStdString();
    }
    EXPECT_EQ(QDir(run.filePath(QStringLiteral("report_previews/calibration"))).entryList(QDir::Files).size(),
              scenario->facts.calibrationMasters);
}

TEST(ReportScenarios, AHistoryTooSmallForTheMastersKeepsTheRunsMetadataAlone)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFiltersWithoutPayloads);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_GT(scenario->historyByteLimit, 0);
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    const QDir run(scenario->runDirectory);
    EXPECT_FALSE(archivedManifestRoot(scenario->runDirectory).isEmpty());
    EXPECT_TRUE(run.exists(QStringLiteral("workflow_report.html")));
    EXPECT_FALSE(run.exists(QStringLiteral("registration_metrics.db"))) << "the snapshot is a payload";
    for (const IntegrationGroupResult& group : scenario->result.integrationGroupResults)
    {
        SCOPED_TRACE(group.groupKey.toStdString());
        EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
        EXPECT_FALSE(group.outputRetentionReason.isEmpty());
    }
}

TEST(ReportScenarios, TheTwoThousandLightsAreNamedAndNotWritten)
{
    const std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::TwoThousandLights);
    ASSERT_TRUE(scenario.has_value());
    EXPECT_EQ(scenario->facts.lightsTaken, 2000);
    EXPECT_EQ(scenario->facts.registrationFailures, 40);
    EXPECT_EQ(scenario->facts.lightsCombined, 1960);
    ASSERT_EQ(scenario->manifest.integrationGroups.size(), 1u);
    const std::vector<FrameId>& lights = scenario->manifest.integrationGroups.front().lightFiles;
    ASSERT_EQ(lights.size(), 2000u);
    EXPECT_FALSE(QFileInfo::exists(lights.front().canonicalPath()));
    EXPECT_FALSE(QFileInfo::exists(lights.back().canonicalPath()));
}

TEST(ReportScenarios, EachScenarioStatesTheThingItIsFor)
{
    const auto built = [](ReportScenarioId id) {
        std::optional<ReportScenario> scenario = buildReportScenario(id);
        EXPECT_TRUE(scenario.has_value()) << nameOf(id);
        return scenario;
    };

    if (const auto scenario = built(ReportScenarioId::OneMonoMaster))
    {
        EXPECT_EQ(scenario->facts.mastersPlanned, 1);
        EXPECT_EQ(scenario->facts.calibrationMasters, 0);
        EXPECT_TRUE(scenario->manifest.lightsCalibration.empty());
        EXPECT_TRUE(scenario->manifest.preCalibratedLightGroups.empty());
        EXPECT_EQ(scenario->facts.nights, 1);
    }
    if (const auto scenario = built(ReportScenarioId::OneColourMaster))
    {
        EXPECT_EQ(scenario->facts.mastersPlanned, 1);
        EXPECT_EQ(scenario->facts.nights, 2);
        EXPECT_EQ(scenario->facts.calibrationMasters, 3);
        EXPECT_TRUE(scenario->manifest.integrationGroups.front().filter.isEmpty());
        EXPECT_EQ(scenario->manifest.registrationGroups.front().settings.cfaPattern, QStringLiteral("RGGB"));
        EXPECT_EQ(scenario->result.integrationGroupResults.front().masterSolution, MasterSolutionOutcome::Solved);
        EXPECT_TRUE(scenario->result.warnings.isEmpty()) << "a clean run";
    }
    if (const auto scenario = built(ReportScenarioId::TwoTargets))
    {
        EXPECT_EQ(scenario->facts.mastersMade, 4);
        EXPECT_EQ(scenario->facts.registrationGroups, 2);
        QSet<QString> targets;
        for (const WorkflowManifest::IntegrationGroup& group : scenario->manifest.integrationGroups)
        {
            targets.insert(group.object);
        }
        EXPECT_EQ(targets.size(), 2);
    }
    if (const auto scenario = built(ReportScenarioId::DrizzledGroup))
    {
        ASSERT_EQ(scenario->manifest.integrationGroups.size(), 2u);
        const WorkflowManifest::IntegrationGroup& drizzled = scenario->manifest.integrationGroups.front();
        ASSERT_TRUE(drizzled.drizzle.has_value());
        EXPECT_TRUE(drizzled.drizzle->enabled);
        EXPECT_FALSE(scenario->manifest.integrationGroups.back().drizzle.has_value());
        EXPECT_TRUE(scenario->manifest.stageSequence.contains(QStringLiteral("LightsDrizzle")));
        // The drizzled master is twice the frames' size, so four times the plain one's samples.
        EXPECT_GT(scenario->result.integrationGroupResults.front().outputBytes,
                  3 * scenario->result.integrationGroupResults.back().outputBytes);
    }
    if (const auto scenario = built(ReportScenarioId::CometRun))
    {
        const RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
        EXPECT_EQ(settings.registrationMethod, QStringLiteral("comet_tracking"));
        EXPECT_TRUE(settings.cometTracking.has_value());
        EXPECT_EQ(scenario->manifest.integrationGroups.front().stacking.cometStackingMode, QStringLiteral("Comet"));
    }
    if (const auto scenario = built(ReportScenarioId::HdrExposureMerge))
    {
        const WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
        EXPECT_TRUE(group.stacking.hdr.enabled);
        QSet<int> exposures;
        for (const double seconds : group.lightExposureSeconds)
        {
            exposures.insert(static_cast<int>(seconds));
        }
        EXPECT_EQ(exposures, (QSet<int>{5, 30, 180}));
    }
    if (const auto scenario = built(ReportScenarioId::PreCalibratedAndSuppliedDark))
    {
        ASSERT_EQ(scenario->manifest.preCalibratedLightGroups.size(), 1u);
        EXPECT_EQ(scenario->manifest.preCalibratedLightGroups.front().lightFiles.size(), 12u);
        EXPECT_EQ(scenario->manifest.lightsCalibration.size(), 16u);
        ASSERT_TRUE(scenario->manifest.darkStage.has_value());
        const WorkflowManifest::CalibrationStage::Group& dark = scenario->manifest.darkStage->groups.front();
        ASSERT_TRUE(dark.existingMaster.has_value());
        EXPECT_TRUE(dark.skipIfExists);
        EXPECT_EQ(dark.inputFiles.size(), 1u);
        EXPECT_TRUE(QFileInfo::exists(dark.existingMaster->canonicalPath()));
        // A supplied master is outside the output folder and has a result naming it.
        EXPECT_FALSE(
            dark.existingMaster->canonicalPath().startsWith(QFileInfo(scenario->outputDirectory).canonicalFilePath()));
        ASSERT_EQ(scenario->result.calibrationGroupResults.size(), 1u);
        EXPECT_EQ(FrameId(scenario->result.calibrationGroupResults.front().masterPath), *dark.existingMaster);
    }
    if (const auto scenario = built(ReportScenarioId::LiteFrontEnd))
    {
        EXPECT_EQ(scenario->manifest.processLabel, QStringLiteral("Image Integration Lite"));
        EXPECT_EQ(scenario->facts.mastersPlanned, 1);
    }
    if (const auto scenario = built(ReportScenarioId::PhotometricAndLocalWeighting))
    {
        ASSERT_EQ(scenario->result.integrationGroupResults.size(), 2u);
        EXPECT_EQ(scenario->manifest.integrationGroups.front().stacking.weighting, QStringLiteral("Photometric"));
        EXPECT_TRUE(scenario->result.integrationGroupResults.front().qualityReport.hasWeightingStats);
        EXPECT_FALSE(scenario->result.integrationGroupResults.front().qualityReport.hasLocalWeightingStats);
        EXPECT_TRUE(scenario->result.integrationGroupResults.back().qualityReport.hasLocalWeightingStats);
    }
    if (const auto scenario = built(ReportScenarioId::DistortionCorrectionsAndManualPoints))
    {
        ASSERT_EQ(scenario->manifest.registrationGroups.size(), 2u);
        EXPECT_TRUE(scenario->manifest.registrationGroups.front().settings.chromaticAberration.enabled);
        const RegistrationSettings& manual = scenario->manifest.registrationGroups.back().settings;
        EXPECT_EQ(manual.registrationMethod, QStringLiteral("manual_points"));
        EXPECT_EQ(manual.manualControlPointsByFrame.size(), 11);
        ASSERT_EQ(scenario->result.alignmentGroupResults.size(), 2u);
        EXPECT_EQ(scenario->result.alignmentGroupResults.front().sipCorrectedCount, 20);
        EXPECT_EQ(scenario->result.alignmentGroupResults.front().caCorrectedCount, 17);
        EXPECT_EQ(scenario->result.alignmentGroupResults.front().caPassthroughCount, 3);
        EXPECT_EQ(scenario->result.alignmentGroupResults.back().sipCorrectedCount, 0);
    }
    if (const auto scenario = built(ReportScenarioId::FiveFiltersWithoutMetrics))
    {
        EXPECT_FALSE(
            QFileInfo::exists(QDir(scenario->outputDirectory).filePath(QStringLiteral("registration_metrics.db"))));
        EXPECT_EQ(scenario->facts.mastersMade, 6);
    }
}

TEST(ReportScenarios, TheReportARunWritesTodayIsBuiltFromEveryScenario)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(nameOf(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());

        const ImageIntegration::Reporting::WorkflowReportBuilder builder(
            scenario->manifest, scenario->result, QString(), scenario->context, nullptr, scenario->session());
        const QString report = builder.build();

        EXPECT_TRUE(report.contains(scenario->manifest.executionId));
        for (const IntegrationGroupResult& group : scenario->result.integrationGroupResults)
        {
            if (group.success)
            {
                EXPECT_TRUE(report.contains(group.outputFilename)) << group.outputFilename.toStdString();
            }
        }
        for (const QString& warning : scenario->result.warnings)
        {
            // The report states every warning of the run; a long one is checked by how it starts.
            EXPECT_TRUE(report.contains(warning.left(60).toHtmlEscaped()) || report.contains(warning.left(60)))
                << warning.toStdString();
        }
    }
}
