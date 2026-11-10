// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights a report model states: one frame per light of each group, with what the registration recorded of it
/// and - for a light it registered - the weight the stack gave it, its quality score and its share of the stack.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
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

/// The result of a master's group, by its key; null when the run has none.
const IntegrationGroupResult* resultOf(const ReportScenario& scenario, const QString& key)
{
    const auto found =
        std::find_if(scenario.result.integrationGroupResults.begin(), scenario.result.integrationGroupResults.end(),
                     [&key](const IntegrationGroupResult& group) { return group.groupKey == key; });
    return found == scenario.result.integrationGroupResults.end() ? nullptr : &*found;
}
} // namespace

TEST(ReportFrameFacts, EveryLightOfAGroupIsAFrameOfItsMaster)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());

        const ReportModel model = buildScenarioModel(*scenario);

        int frames = 0;
        for (const WorkflowManifest::IntegrationGroup& group : scenario->manifest.integrationGroups)
        {
            const QString key = group.groupKey.toString();
            const auto master = std::find_if(model.masters.begin(), model.masters.end(),
                                             [&key](const ReportMaster& candidate) { return candidate.key == key; });
            ASSERT_NE(master, model.masters.end()) << key.toStdString();
            ASSERT_EQ(master->frames.size(), group.lightFiles.size());
            EXPECT_EQ(master->exposure.frames, static_cast<int>(group.lightFiles.size()));
            for (std::size_t i = 0; i < group.lightFiles.size(); ++i)
            {
                // The frames are in the order of the group's lights, which the stack's own records are not.
                const ReportFrame& frame = master->frames[i];
                EXPECT_EQ(frame.path, group.lightFiles[i].canonicalPath());
                EXPECT_EQ(frame.fileName, QFileInfo(frame.path).fileName());
                ASSERT_TRUE(frame.exposureSeconds.has_value());
                EXPECT_DOUBLE_EQ(*frame.exposureSeconds, group.lightExposureSeconds[i]);
            }
            frames += static_cast<int>(master->frames.size());
        }
        EXPECT_EQ(frames, model.run.frames);
    }
}

TEST(ReportFrameFacts, AFrameStatesWhatItsRegistrationRecorded)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster* master = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);

    ASSERT_TRUE(master->weighting.has_value());
    EXPECT_EQ(master->weighting->framesWithMetrics, 60);
    EXPECT_EQ(master->weighting->framesRegistered, 60);
    for (const ReportFrame& frame : master->frames)
    {
        SCOPED_TRACE(frame.fileName.toStdString());
        ASSERT_TRUE(frame.registered.has_value());
        EXPECT_TRUE(*frame.registered);
        ASSERT_TRUE(frame.stars.has_value());
        EXPECT_GT(*frame.stars, 0);
        ASSERT_TRUE(frame.fwhm.has_value());
        EXPECT_GT(*frame.fwhm, 0.0);
        ASSERT_TRUE(frame.snr.has_value());
        ASSERT_TRUE(frame.eccentricity.has_value());
        EXPECT_FALSE(frame.transformKind.isEmpty());
        ASSERT_TRUE(frame.weight.has_value());
        EXPECT_GT(*frame.weight, 0.0);
        ASSERT_TRUE(frame.qualityScore.has_value());
        EXPECT_GE(*frame.qualityScore, 0.0);
        EXPECT_LE(*frame.qualityScore, 100.0);
    }
    ASSERT_TRUE(master->weighting->medianQualityScore.has_value());
    ASSERT_TRUE(master->weighting->medianWeight.has_value());
    EXPECT_GT(master->weighting->weightedIntegrationMs, 0);
}

TEST(ReportFrameFacts, TheWeightsAreTheOnesTheStackRecordedCombiningEachLightWith)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster* master = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);
    const IntegrationGroupResult* result = resultOf(*scenario, master->key);
    ASSERT_NE(result, nullptr);
    const std::vector<StackLightWeight>& recorded = result->qualityReport.lightWeights;
    ASSERT_EQ(recorded.size(), master->frames.size());

    // The stack records each weight by the light's name, in its own order; the model shows them relative to the
    // heaviest light.
    double heaviest = 0.0;
    QHash<QString, double> byLight;
    for (const StackLightWeight& entry : recorded)
    {
        heaviest = (std::max)(heaviest, entry.weight);
        byLight.insert(FrameId(entry.light).canonicalPath(), entry.weight);
    }
    ASSERT_GT(heaviest, 0.0);
    double largest = 0.0;
    for (const ReportFrame& frame : master->frames)
    {
        ASSERT_TRUE(byLight.contains(frame.path)) << frame.path.toStdString();
        ASSERT_TRUE(frame.weight.has_value());
        EXPECT_NEAR(*frame.weight, byLight.value(frame.path) / heaviest, 1e-12);
        largest = (std::max)(largest, *frame.weight);
    }
    EXPECT_DOUBLE_EQ(largest, 1.0);
}

TEST(ReportFrameFacts, APhotometricWeightIsShownAtItsMeanOfOneAndStatesItsPhotometry)
{
    const auto scenario = built(ReportScenarioId::PhotometricAndLocalWeighting);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);
    ASSERT_EQ(model.masters.size(), 2u);
    const ReportMaster& photometric = model.masters.front();
    const IntegrationGroupResult* result = resultOf(*scenario, photometric.key);
    ASSERT_NE(result, nullptr);

    ASSERT_TRUE(photometric.weighting.has_value());
    EXPECT_EQ(photometric.weighting->mode, QStringLiteral("Photometric"));
    EXPECT_FALSE(photometric.weighting->floorApplies) << "the photometric floor is on another scale than the one shown";
    EXPECT_EQ(photometric.weighting->framesAtFloor, 0);
    ASSERT_TRUE(photometric.weighting->medianZeroPoint.has_value());
    ASSERT_TRUE(photometric.weighting->medianCatalogMatches.has_value());

    QHash<QString, double> byLight;
    for (const StackLightWeight& entry : result->qualityReport.lightWeights)
    {
        byLight.insert(FrameId(entry.light).canonicalPath(), entry.weight);
    }
    for (const ReportFrame& frame : photometric.frames)
    {
        if (!frame.weight.has_value())
        {
            continue;
        }
        ASSERT_TRUE(byLight.contains(frame.path));
        EXPECT_NEAR(*frame.weight, byLight.value(frame.path), 1e-12) << "as the stack recorded it, not rescaled";
        EXPECT_TRUE(frame.zeroPoint.has_value());
        EXPECT_TRUE(frame.catalogMatches.has_value());
    }

    // The stack's own record of the weights it gave per pixel is on the master, not on its lights.
    const ReportMaster& local = model.masters.back();
    ASSERT_TRUE(local.stack.has_value());
    ASSERT_TRUE(local.stack->localWeights.has_value());
    EXPECT_GT(local.stack->localWeights->grids, 0u);
    EXPECT_FALSE(photometric.stack->localWeights.has_value());
}

TEST(ReportFrameFacts, TheSharesOfAStackAddUpToTheWholeOfIt)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportMaster& master : model.masters)
    {
        if (!master.weighting.has_value())
        {
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        double share = 0.0;
        double weightedSeconds = 0.0;
        for (const ReportFrame& frame : master.frames)
        {
            share += frame.stackShare.value_or(0.0);
            weightedSeconds += frame.weight.value_or(0.0) * frame.exposureSeconds.value_or(0.0);
        }
        EXPECT_NEAR(share, 1.0, 1e-9);
        EXPECT_NEAR(static_cast<double>(master.weighting->weightedIntegrationMs) / 1000.0, weightedSeconds, 0.5);
    }
}

TEST(ReportFrameFacts, ALightWhoseRegistrationFailedIsAFrameWithNoWeight)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_GT(scenario->facts.registrationFailures, 0);

    const ReportModel model = buildScenarioModel(*scenario);

    int failed = 0;
    int registered = 0;
    for (const ReportMaster& master : model.masters)
    {
        int ofMaster = 0;
        for (const ReportFrame& frame : master.frames)
        {
            if (!frame.registered.has_value())
            {
                continue;
            }
            if (*frame.registered)
            {
                ++ofMaster;
                continue;
            }
            ++failed;
            // It is still one of the group's lights, with what the registration saw of it, and no part in the stack.
            EXPECT_TRUE(frame.stars.has_value());
            EXPECT_FALSE(frame.weight.has_value());
            EXPECT_FALSE(frame.stackShare.has_value());
            EXPECT_FALSE(frame.qualityScore.has_value());
        }
        registered += ofMaster;
        if (master.weighting.has_value())
        {
            EXPECT_EQ(master.weighting->framesRegistered, ofMaster);
        }
    }
    EXPECT_EQ(failed, scenario->facts.registrationFailures);
    EXPECT_GT(registered, 0);
}

TEST(ReportFrameFacts, ALightARestackLeftOutWeighsNothingInTheMaster)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    int leftOut = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (!master.stack.has_value() || !master.stack->restack.has_value() || !master.stack->restack->applied)
        {
            continue;
        }
        for (const ReportRestackExclusion& excluded : master.stack->restack->excluded)
        {
            ASSERT_LT(excluded.index, master.frames.size());
            const ReportFrame& frame = master.frames[excluded.index];
            ++leftOut;
            // The stack recorded no weight for a light the master does not combine.
            EXPECT_DOUBLE_EQ(frame.weight.value_or(0.0), 0.0) << frame.fileName.toStdString();
            EXPECT_FALSE(frame.stackShare.has_value());
        }
    }
    EXPECT_EQ(leftOut, scenario->facts.lightsExcludedOnRestack);
}

TEST(ReportFrameFacts, WithoutAMetricsDatabaseAFrameStatesItsFileAndItsExposureAlone)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    int frames = 0;
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.weighting.has_value()) << master.name.toStdString();
        for (const ReportFrame& frame : master.frames)
        {
            ++frames;
            EXPECT_FALSE(frame.fileName.isEmpty());
            EXPECT_TRUE(frame.exposureSeconds.has_value());
            EXPECT_FALSE(frame.registered.has_value());
            EXPECT_FALSE(frame.stars.has_value());
            EXPECT_FALSE(frame.fwhm.has_value());
            EXPECT_FALSE(frame.weight.has_value());
            EXPECT_FALSE(frame.qualityScore.has_value());
            EXPECT_TRUE(frame.transformKind.isEmpty());
        }
    }
    EXPECT_EQ(frames, 174);
}

TEST(ReportFrameFacts, TheWeightingAStackAppliedIsNamedAndItsFloorShownOnlyWhereItApplies)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.minWeightFloor = 0.2;

    group.stacking.weighting = QStringLiteral("Number of Stars");
    const ReportMaster byStars = buildScenarioModel(*scenario).masters.front();
    ASSERT_TRUE(byStars.weighting.has_value());
    EXPECT_EQ(byStars.weighting->mode, QStringLiteral("Number of Stars"));
    EXPECT_TRUE(byStars.weighting->floorApplies);
    EXPECT_DOUBLE_EQ(byStars.weighting->minimumWeight, 0.2);

    group.stacking.weighting = QStringLiteral("Noise");
    const ReportMaster byNoise = buildScenarioModel(*scenario).masters.front();
    ASSERT_TRUE(byNoise.weighting.has_value());
    EXPECT_EQ(byNoise.weighting->mode, QStringLiteral("Noise"));
    EXPECT_FALSE(byNoise.weighting->floorApplies) << "a noise weight takes no floor";
    EXPECT_EQ(byNoise.weighting->framesAtFloor, 0);
}

TEST(ReportFrameFacts, AWeightingTheStackDidNotRecordIsDerivedFromWhatTheRegistrationRecorded)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.integrationGroupResults.front().qualityReport.lightWeights.clear();
    scenario->manifest.integrationGroups.front().stacking.weighting = QStringLiteral("Number of Stars");

    const ReportMaster master = buildScenarioModel(*scenario).masters.front();

    QSet<int> stars;
    QSet<QString> weights;
    double heaviest = 0.0;
    for (const ReportFrame& frame : master.frames)
    {
        if (!frame.registered.value_or(false))
        {
            continue;
        }
        ASSERT_TRUE(frame.weight.has_value());
        EXPECT_GT(*frame.weight, 0.0);
        EXPECT_LE(*frame.weight, 1.0);
        heaviest = (std::max)(heaviest, *frame.weight);
        stars.insert(frame.stars.value_or(0));
        weights.insert(QString::number(*frame.weight, 'g', 12));
    }
    // The weights are relative to the heaviest light, and lights of different star counts weigh differently.
    EXPECT_DOUBLE_EQ(heaviest, 1.0);
    ASSERT_GT(stars.size(), 1) << "the scenario's lights do not all have the same stars";
    EXPECT_GT(weights.size(), 1) << "every light weighs the same: the weights were not derived";
}

TEST(ReportFrameFacts, AStackThatRecordedNoWeightsIsWeighedAsItsSettingsImply)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    // A run of a version that recorded no weights: the stack's record names none.
    scenario->result.integrationGroupResults.front().qualityReport.lightWeights.clear();
    scenario->manifest.integrationGroups.front().stacking.weighting = QStringLiteral("None");

    const ReportMaster master = buildScenarioModel(*scenario).masters.front();

    ASSERT_TRUE(master.weighting.has_value());
    EXPECT_EQ(master.weighting->mode, QStringLiteral("None"));
    for (const ReportFrame& frame : master.frames)
    {
        if (frame.registered.value_or(false))
        {
            ASSERT_TRUE(frame.weight.has_value());
            EXPECT_DOUBLE_EQ(*frame.weight, 1.0) << "with no weighting every light weighs the same";
        }
    }
}
