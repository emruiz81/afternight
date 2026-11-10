// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run beside the earlier runs of its output folder: the report model compares a run's totals with those of the
/// most recent earlier run that completed, and states no comparison where there is none to make.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

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

int lightsOf(const WorkflowManifest& manifest)
{
    int lights = 0;
    for (const WorkflowManifest::IntegrationGroup& group : manifest.integrationGroups)
    {
        lights += static_cast<int>(group.lightFiles.size());
    }
    return lights;
}
} // namespace

TEST(ReportComparison, ARunIsComparedWithTheEarlierRunOfItsFolder)
{
    auto scenario = built(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    const ReportComparison& comparison = *model.comparison;
    EXPECT_EQ(comparison.baselineRunId, QFileInfo(scenario->earlierRunDirectories.front()).fileName());
    EXPECT_NE(comparison.baselineRunId, QFileInfo(scenario->runDirectory).fileName())
        << "a run is not its own baseline";
    EXPECT_TRUE(comparison.baselineArchivedAt.has_value());

    // The earlier run was of the first night alone.
    EXPECT_EQ(comparison.current.frames, lightsOf(scenario->manifest));
    EXPECT_EQ(comparison.current.frames, 174);
    EXPECT_EQ(comparison.previous.frames, 87);
    EXPECT_EQ(comparison.current.integrationMs, model.run.capturedMs);
    EXPECT_GT(comparison.previous.integrationMs, 0);
    EXPECT_LT(comparison.previous.integrationMs, comparison.current.integrationMs);

    qint64 bytes = 0;
    for (const ReportMaster& master : model.masters)
    {
        bytes += master.bytes.value_or(0);
    }
    EXPECT_EQ(comparison.current.outputBytes, bytes);
    EXPECT_GT(comparison.previous.outputBytes, 0);

    // This run's masters were measured, so its totals state what they measured.
    ASSERT_TRUE(comparison.current.snr.has_value());
    EXPECT_GT(*comparison.current.snr, 0.0);
    ASSERT_TRUE(comparison.current.overlapRetained.has_value());
    EXPECT_LE(*comparison.current.overlapRetained, 1.0);
}

TEST(ReportComparison, ARunWithNoEarlierRunStatesNoComparison)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(scenario->earlierRunDirectories.isEmpty());
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    EXPECT_FALSE(buildScenarioModel(*scenario, scenario->runDirectory).comparison.has_value());
}

TEST(ReportComparison, ARunThatHasNoFolderOfItsOwnIsComparedWithNothing)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());

    EXPECT_FALSE(buildScenarioModel(*scenario).comparison.has_value());
}

TEST(ReportComparison, AnEarlierRunThatDidNotCompleteIsNoBaseline)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));
    const QDir history = QFileInfo(scenario->runDirectory).dir();

    // An earlier run that was cancelled, and one whose manifest cannot be read.
    ASSERT_TRUE(writeArchivedRunManifest(history.filePath(QStringLiteral("20200101_000000_000")), scenario->manifest,
                                         QStringLiteral("cancelled"), QJsonArray{}));
    ASSERT_TRUE(QDir().mkpath(history.filePath(QStringLiteral("20200102_000000_000"))));
    EXPECT_FALSE(buildScenarioModel(*scenario, scenario->runDirectory).comparison.has_value());

    // One that completed is.
    WorkflowManifest earlier = scenario->manifest;
    earlier.integrationGroups.front().lightFiles.pop_back();
    earlier.integrationGroups.front().lightExposureSeconds.pop_back();
    ASSERT_TRUE(writeArchivedRunManifest(history.filePath(QStringLiteral("20200103_000000_000")), earlier,
                                         QStringLiteral("success"), QJsonArray{}));
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    ASSERT_TRUE(model.comparison.has_value());
    EXPECT_EQ(model.comparison->baselineRunId, QStringLiteral("20200103_000000_000"));
    EXPECT_EQ(model.comparison->previous.frames, model.comparison->current.frames - 1);
    // The earlier run's archive names no master: no size to add up, and nothing measured to state.
    EXPECT_EQ(model.comparison->previous.outputBytes, 0);
    EXPECT_FALSE(model.comparison->previous.snr.has_value());
    EXPECT_FALSE(model.comparison->previous.overlapRetained.has_value());
}
