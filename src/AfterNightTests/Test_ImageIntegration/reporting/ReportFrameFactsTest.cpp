// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights a report model states: one frame per light of each group, with what the registration recorded of it
/// and - for a light it registered - the weight the stack gave it, its quality score and its share of the stack.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
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

int framesOfState(const std::vector<ReportFrame>& frames, const ReportFrameState state)
{
    return static_cast<int>(std::count_if(frames.begin(), frames.end(), [state](const ReportFrame& frame) {
        return frame.state.has_value() && *frame.state == state;
    }));
}

/// The lights of a state: the masters' and those of the groups the run planned no master of.
int framesOfState(const ReportModel& model, const ReportFrameState state)
{
    int frames = 0;
    for (const ReportMaster& master : model.masters)
    {
        frames += framesOfState(master.frames, state);
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        frames += framesOfState(group.frames, state);
    }
    return frames;
}

bool leftOutByUser(const ReportFrame& frame)
{
    return frame.state == ReportFrameState::ExcludedByUser || frame.state == ReportFrameState::StackExcludedByUser;
}

bool namesNotStated(const ReportModel& model, const QString& fragment)
{
    return std::any_of(model.notStated.begin(), model.notStated.end(), [&fragment](const ReportNotStated& entry) {
        return entry.what.contains(fragment) || entry.why.contains(fragment);
    });
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
            // The group's lights, then the ones the user left out of the group.
            const auto leftOut = static_cast<std::size_t>(std::count_if(
                scenario->manifest.leftOutLights.begin(), scenario->manifest.leftOutLights.end(),
                [&group](const WorkflowManifest::LeftOutLight& light) { return light.groupKey == group.groupKey; }));
            ASSERT_EQ(master->frames.size(), group.lightFiles.size() + leftOut);
            for (std::size_t i = group.lightFiles.size(); i < master->frames.size(); ++i)
            {
                EXPECT_TRUE(leftOutByUser(master->frames[i])) << master->frames[i].fileName.toStdString();
            }
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
        // The lights of a group the user left out whole are frames too, under the group's key.
        for (const ReportGroupNotStacked& group : model.groupsNotStacked)
        {
            EXPECT_FALSE(group.frames.empty());
            frames += static_cast<int>(group.frames.size());
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
            // A light the user left out of the stack is no light of the group the weighting counts.
            if (!frame.registered.has_value() || leftOutByUser(frame))
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

// ---- What became of each light.

TEST(ReportFrameFacts, EveryLightOfAStackHasOneStateAndTheStatesAddUpToTheRunsFacts)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportScenarioFacts& facts = scenario->facts;

        const ReportModel model = buildScenarioModel(*scenario);

        int notStacked = 0;
        for (const ReportMaster& master : model.masters)
        {
            for (const ReportFrame& frame : master.frames)
            {
                if (leftOutByUser(frame))
                {
                    // A light the user left out is in no stack, whatever became of its group's master.
                    EXPECT_FALSE(frame.weight.has_value());
                    EXPECT_FALSE(frame.stackShare.has_value());
                    EXPECT_FALSE(frame.stateDetail.isEmpty());
                    continue;
                }
                if (master.state == ReportMasterState::Reused)
                {
                    // What became of a reused master's lights is the record of the run that made it.
                    EXPECT_TRUE(!frame.state.has_value() || *frame.state == ReportFrameState::RegistrationFailed);
                    continue;
                }
                ASSERT_TRUE(frame.state.has_value()) << frame.fileName.toStdString();
                if (master.state != ReportMasterState::Made)
                {
                    EXPECT_TRUE(*frame.state == ReportFrameState::NotStacked ||
                                *frame.state == ReportFrameState::RegistrationFailed);
                    notStacked += *frame.state == ReportFrameState::NotStacked ? 1 : 0;
                    EXPECT_FALSE(frame.stateDetail.isEmpty());
                }
                if (*frame.state != ReportFrameState::Used)
                {
                    EXPECT_FALSE(frame.stackShare.has_value());
                }
            }
        }
        EXPECT_EQ(framesOfState(model, ReportFrameState::Used), facts.lightsCombined);
        EXPECT_EQ(framesOfState(model, ReportFrameState::RegistrationFailed), facts.registrationFailures);
        EXPECT_EQ(framesOfState(model, ReportFrameState::LeftOutByStack), facts.lightsDroppedByStack);
        EXPECT_EQ(framesOfState(model, ReportFrameState::ExcludedOnRestack), facts.lightsExcludedOnRestack);
        EXPECT_EQ(framesOfState(model, ReportFrameState::NotStacked), notStacked);
        // The two states the manifest's list of the lights left out tells.
        EXPECT_EQ(framesOfState(model, ReportFrameState::ExcludedByUser) +
                      framesOfState(model, ReportFrameState::StackExcludedByUser),
                  facts.lightsLeftOutByUser);
        EXPECT_EQ(model.run.frames, facts.lightsTaken);

        if (facts.lightsCombined > 0)
        {
            ASSERT_TRUE(model.run.framesUsed.has_value());
            EXPECT_EQ(*model.run.framesUsed, facts.lightsCombined);
            ASSERT_TRUE(model.run.integratedMs.has_value());
            EXPECT_GT(*model.run.integratedMs, 0);
            EXPECT_LE(*model.run.integratedMs, model.run.capturedMs);
        }
        int counted = 0;
        for (const ReportFrameStateCount& count : model.run.frameStates)
        {
            EXPECT_GT(count.frames, 0) << "a fate no light met is not listed";
            EXPECT_EQ(count.frames, framesOfState(model, count.state));
            counted += count.frames;
        }
        EXPECT_LE(counted, model.run.frames);
        // The run's count of each stack agrees with its records, so nothing is named as not stated about it.
        EXPECT_FALSE(namesNotStated(model, QStringLiteral("Which lights the master")));
    }
}

TEST(ReportFrameFacts, ALightTheStackDroppedWasLeftOutByTheStackForTheReasonItsResultStates)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->facts.lightsDroppedByStack, 1);
    const IntegrationGroupResult* result =
        resultOf(*scenario, scenario->manifest.integrationGroups.front().groupKey.toString());
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->droppedLights.size(), 1u);

    const ReportModel model = buildScenarioModel(*scenario);

    int leftOut = 0;
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        if (frame.state != ReportFrameState::LeftOutByStack)
        {
            continue;
        }
        ++leftOut;
        // It is the light the group's result names, its registration succeeded, and the stack's weights do not
        // name it; the detail gives the reason the result recorded.
        EXPECT_EQ(FrameId(frame.path).identityKey(), FrameId(result->droppedLights.front().light).identityKey());
        EXPECT_TRUE(frame.registered.value_or(false));
        EXPECT_FALSE(frame.weight.has_value());
        EXPECT_FALSE(frame.rejectedFraction.has_value());
        EXPECT_TRUE(frame.stateDetail.contains(QStringLiteral("no registered image")))
            << frame.stateDetail.toStdString();
    }
    EXPECT_EQ(leftOut, 1);

    // A light whose registration failed is told apart from it, with the stars its detection found.
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        if (frame.state == ReportFrameState::RegistrationFailed)
        {
            ASSERT_TRUE(frame.stars.has_value());
            EXPECT_TRUE(frame.stateDetail.contains(QString::number(*frame.stars))) << frame.stateDetail.toStdString();
            EXPECT_FALSE(frame.shiftX.has_value());
        }
    }
}

TEST(ReportFrameFacts, EachReasonAStackDropsALightForIsASentenceOfItsOwn)
{
    auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    ASSERT_EQ(result.droppedLights.size(), 1u);

    QSet<QString> sentences;
    for (const DroppedLightReason reason :
         {DroppedLightReason::NoRegisteredOutput, DroppedLightReason::NoRegistrationTransform,
          DroppedLightReason::NoStarBaseProduct, DroppedLightReason::NoCometAlignedImage})
    {
        result.droppedLights.front().reason = reason;
        const ReportModel model = buildScenarioModel(*scenario);
        for (const ReportFrame& frame : model.masters.front().frames)
        {
            if (frame.state == ReportFrameState::LeftOutByStack)
            {
                sentences.insert(frame.stateDetail);
            }
        }
    }
    EXPECT_EQ(sentences.size(), 4);
}

// A light is left out by the stack when the group's result names it, and by nothing else: a registered light the
// stack's weights do not name, which no record names either, has no state, and the model says so.
TEST(ReportFrameFacts, ALightNoRecordOfTheRunNamesHasNoStateAndTheModelSaysSo)
{
    auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    ASSERT_EQ(result.droppedLights.size(), 1u);
    const QString dropped = FrameId(result.droppedLights.front().light).identityKey();
    result.droppedLights.clear();

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(framesOfState(model, ReportFrameState::LeftOutByStack), 0);
    int unnamed = 0;
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        if (!frame.state.has_value())
        {
            ++unnamed;
            EXPECT_EQ(FrameId(frame.path).identityKey(), dropped);
            EXPECT_FALSE(frame.weight.has_value());
        }
    }
    EXPECT_EQ(unnamed, 1);
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("recorded no reason for leaving")));
    EXPECT_EQ(framesOfState(model, ReportFrameState::Used), scenario->facts.lightsCombined);
}

TEST(ReportFrameFacts, TheLightsTheUserLeftOutTakeTheStateOfTheirExclusionUnderTheGroupTheyWouldHaveJoined)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_FALSE(scenario->lightsLeftOutByUser.empty());
    ASSERT_EQ(scenario->manifest.leftOutLights.size(), scenario->lightsLeftOutByUser.size());

    const ReportModel model = buildScenarioModel(*scenario);

    int underAMaster = 0;
    int underNoMaster = 0;
    int fromStack = 0;
    for (const ReportScenarioLeftOutLight& light : scenario->lightsLeftOutByUser)
    {
        SCOPED_TRACE(light.path.toStdString());
        const QString path = FrameId(light.path).canonicalPath();
        const std::vector<ReportFrame>* frames = nullptr;
        const auto master = std::find_if(model.masters.begin(), model.masters.end(), [&light](const ReportMaster& m) {
            return m.key == light.integrationGroupKey;
        });
        if (master != model.masters.end())
        {
            frames = &master->frames;
            ++underAMaster;
        }
        else
        {
            const auto group =
                std::find_if(model.groupsNotStacked.begin(), model.groupsNotStacked.end(),
                             [&light](const ReportGroupNotStacked& g) { return g.key == light.integrationGroupKey; });
            ASSERT_NE(group, model.groupsNotStacked.end()) << "a light left out is listed under its group's key";
            EXPECT_FALSE(group->name.isEmpty());
            frames = &group->frames;
            ++underNoMaster;
        }
        const auto frame = std::find_if(frames->begin(), frames->end(),
                                        [&path](const ReportFrame& candidate) { return candidate.path == path; });
        ASSERT_NE(frame, frames->end());
        EXPECT_EQ(std::count_if(frames->begin(), frames->end(),
                                [&path](const ReportFrame& candidate) { return candidate.path == path; }),
                  1);
        ASSERT_TRUE(frame->state.has_value());
        EXPECT_EQ(*frame->state,
                  light.fromStackOnly ? ReportFrameState::StackExcludedByUser : ReportFrameState::ExcludedByUser);
        EXPECT_FALSE(frame->stateDetail.isEmpty());
        EXPECT_FALSE(frame->weight.has_value());
        EXPECT_FALSE(frame->fileName.isEmpty());
        if (light.fromStackOnly)
        {
            // A light left out of the stack alone was still registered, and states what the registration measured.
            ++fromStack;
            EXPECT_TRUE(frame->registered.has_value());
            EXPECT_TRUE(frame->stars.has_value());
            EXPECT_TRUE(frame->exposureSeconds.has_value());
            EXPECT_TRUE(frame->night.has_value());
        }
        else
        {
            EXPECT_FALSE(frame->registered.has_value()) << "a light excluded in the lights' list was not registered";
        }
    }
    EXPECT_GT(underAMaster, 0);
    EXPECT_GT(underNoMaster, 0) << "the scenario leaves a whole group out";
    EXPECT_GT(fromStack, 0);

    // The lights left out follow the group's own, whose places the stack's records count by.
    for (const ReportMaster& master : model.masters)
    {
        bool leftOutSeen = false;
        for (const ReportFrame& frame : master.frames)
        {
            leftOutSeen = leftOutSeen || leftOutByUser(frame);
            EXPECT_TRUE(!leftOutSeen || leftOutByUser(frame)) << frame.fileName.toStdString();
        }
    }
    // A light the user left out was never offered to a stack: it is no part of a stack's count.
    EXPECT_FALSE(namesNotStated(model, QStringLiteral("Which lights the master")));
}

TEST(ReportFrameFacts, WithoutAMetricsDatabaseALightWhoseRegistrationFailedHasNoStateAndTheModelSaysSo)
{
    auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    const ProductionReportSources production(scenario->session(), QString());

    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                 .result = scenario->result,
                                                                 .runDirectory = {},
                                                                 .context = scenario->context,
                                                                 .counters = std::nullopt},
                                               production.sources());

    // The lights the stack dropped are the ones its result names; a failed registration is told by the metrics
    // alone, so without them such a light is in no record and nothing is inferred of it.
    EXPECT_EQ(framesOfState(model, ReportFrameState::RegistrationFailed), 0);
    EXPECT_EQ(framesOfState(model, ReportFrameState::LeftOutByStack), scenario->facts.lightsDroppedByStack);
    EXPECT_EQ(framesOfState(model, ReportFrameState::Used), scenario->facts.lightsCombined);
    int unnamed = 0;
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        unnamed += frame.state.has_value() ? 0 : 1;
    }
    EXPECT_EQ(unnamed, scenario->facts.registrationFailures);
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("recorded no reason for leaving")));
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("no registration metrics database")));
}

TEST(ReportFrameFacts, OfAStackThatRecordedNoWeightsNoLightsFateOrWeightIsStated)
{
    auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    result.qualityReport.lightWeights.clear();
    result.qualityReport.frameRejectionRates.clear();

    const ReportModel model = buildScenarioModel(*scenario);

    // The weights are what names the lights a stack combined: without them nothing is assumed of a light, and
    // nothing is derived from the settings in their place.
    EXPECT_EQ(framesOfState(model, ReportFrameState::Used), 0);
    EXPECT_EQ(framesOfState(model, ReportFrameState::LeftOutByStack), scenario->facts.lightsDroppedByStack)
        << "the lights the result names as dropped are stated whatever the weights";
    EXPECT_EQ(framesOfState(model, ReportFrameState::ExcludedOnRestack), 0);
    EXPECT_EQ(framesOfState(model, ReportFrameState::RegistrationFailed), scenario->facts.registrationFailures);
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        EXPECT_FALSE(frame.weight.has_value());
        EXPECT_FALSE(frame.stackShare.has_value());
        const bool named = (frame.registered.has_value() && !*frame.registered) || leftOutByUser(frame) ||
                           frame.state == ReportFrameState::LeftOutByStack;
        EXPECT_TRUE(frame.state.has_value() == named) << frame.fileName.toStdString();
    }
    ASSERT_TRUE(model.masters.front().weighting.has_value());
    EXPECT_FALSE(model.masters.front().weighting->medianWeight.has_value());
    EXPECT_FALSE(model.run.framesUsed.has_value() && *model.run.framesUsed > 0);
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("recorded no weights")));
}

TEST(ReportFrameFacts, ARejectedFractionBelongsToTheLightTheStacksWeightsNameAtTheSamePlace)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    // The model is kept for as long as its master is read.
    const ReportModel asStacked = buildScenarioModel(*scenario);
    const ReportMaster* luminance = masterOfFilter(asStacked, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    const QString key = luminance->key;
    auto resultOfKey =
        std::find_if(scenario->result.integrationGroupResults.begin(), scenario->result.integrationGroupResults.end(),
                     [&key](const IntegrationGroupResult& group) { return group.groupKey == key; });
    ASSERT_NE(resultOfKey, scenario->result.integrationGroupResults.end());
    StackQualityRecord& quality = resultOfKey->qualityReport;
    ASSERT_EQ(quality.frameRejectionRates.size(), quality.lightWeights.size());
    ASSERT_GT(quality.frameRejectionRates.size(), 2u);

    // The stack's own order is not the group's: with it reversed, each light keeps its own rate.
    std::reverse(quality.lightWeights.begin(), quality.lightWeights.end());
    std::reverse(quality.frameRejectionRates.begin(), quality.frameRejectionRates.end());
    QHash<QString, double> rateByLight;
    for (std::size_t i = 0; i < quality.lightWeights.size(); ++i)
    {
        rateByLight.insert(FrameId(quality.lightWeights[i].light).canonicalPath(), quality.frameRejectionRates[i]);
    }

    const ReportModel reversed = buildScenarioModel(*scenario);
    const ReportMaster* master = masterOfFilter(reversed, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);
    ASSERT_EQ(master->frames.size(), quality.lightWeights.size());
    EXPECT_NE(FrameId(quality.lightWeights.front().light).canonicalPath(), master->frames.front().path)
        << "the stack's order is the reverse of the group's";
    for (const ReportFrame& frame : master->frames)
    {
        ASSERT_TRUE(frame.rejectedFraction.has_value()) << frame.fileName.toStdString();
        EXPECT_DOUBLE_EQ(*frame.rejectedFraction, rateByLight.value(frame.path));
    }

    // One rate fewer than the lights weighed: no rate can be given to a light, and the model says so.
    quality.frameRejectionRates.pop_back();
    const ReportModel mismatched = buildScenarioModel(*scenario);
    for (const ReportFrame& frame : masterOfFilter(mismatched, QStringLiteral("Luminance"))->frames)
    {
        EXPECT_FALSE(frame.rejectedFraction.has_value());
    }
    EXPECT_TRUE(namesNotStated(mismatched, QStringLiteral("rejected")));
    EXPECT_FALSE(namesNotStated(reversed, QStringLiteral("rejected")));
}

TEST(ReportFrameFacts, AfterARestackTheKeptLightsHaveTheirOwnRatesAndTheExcludedOnesTheRestacks)
{
    auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    StackQualityRecord& quality = result.qualityReport;
    // The second stack's rates, one per light it kept.
    quality.hasRejectionStats = true;
    quality.frameRejectionRates.clear();
    for (std::size_t i = 0; i < quality.lightWeights.size(); ++i)
    {
        quality.frameRejectionRates.push_back(0.01 + 0.001 * static_cast<double>(i));
    }
    ASSERT_EQ(quality.spatialRestackExcludedFrameIndices.size(), 2u);

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster& master = model.masters.front();

    for (std::size_t i = 0; i < quality.lightWeights.size(); ++i)
    {
        const QString path = FrameId(quality.lightWeights[i].light).canonicalPath();
        const auto frame = std::find_if(master.frames.begin(), master.frames.end(),
                                        [&path](const ReportFrame& candidate) { return candidate.path == path; });
        ASSERT_NE(frame, master.frames.end());
        EXPECT_EQ(frame->state, ReportFrameState::Used);
        ASSERT_TRUE(frame->rejectedFraction.has_value());
        EXPECT_DOUBLE_EQ(*frame->rejectedFraction, quality.frameRejectionRates[i]);
    }
    for (std::size_t i = 0; i < quality.spatialRestackExcludedFrameIndices.size(); ++i)
    {
        // An excluded light is named by its place among the group's lights, and keeps the first stack's rate.
        const ReportFrame& frame = master.frames[quality.spatialRestackExcludedFrameIndices[i]];
        EXPECT_EQ(frame.state, ReportFrameState::ExcludedOnRestack);
        ASSERT_TRUE(frame.rejectedFraction.has_value());
        EXPECT_DOUBLE_EQ(*frame.rejectedFraction, quality.spatialRestackExcludedFrameRates[i]);
        EXPECT_FALSE(frame.stateDetail.isEmpty());
    }
}

// ---- When a light was taken, and how it was brought to the reference.

TEST(ReportFrameFacts, ALightIsOnTheNightItWasTakenInTheArchivesZone)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    // The lights are filed by the evening they were taken on, at a site three hours behind UTC.
    static const QRegularExpression evening(QStringLiteral("/(\\d{4}-\\d{2}-\\d{2})/LIGHT/"));
    QSet<QDate> nights;
    int afterMidnight = 0;
    for (const ReportMaster& master : model.masters)
    {
        for (const ReportFrame& frame : master.frames)
        {
            ASSERT_TRUE(frame.capturedAt.has_value()) << frame.fileName.toStdString();
            ASSERT_TRUE(frame.night.has_value());
            EXPECT_EQ(frame.capturedAt->offsetFromUtc(), -3 * 3600);
            const QRegularExpressionMatch match = evening.match(frame.path);
            ASSERT_TRUE(match.hasMatch()) << frame.path.toStdString();
            EXPECT_EQ(frame.night->toString(Qt::ISODate), match.captured(1));
            afterMidnight += frame.capturedAt->date() != *frame.night ? 1 : 0;
            nights.insert(*frame.night);
        }
    }
    EXPECT_EQ(nights.size(), scenario->facts.nights);
    EXPECT_GT(afterMidnight, 0) << "a light taken after midnight belongs to the evening before";
    ASSERT_EQ(model.run.nights.size(), 2u);
    EXPECT_FALSE(namesNotStated(model, QStringLiteral("on which night")));
}

TEST(ReportFrameFacts, ALightWithNoCaptureTimeIsOnNoNightAndTheModelSaysSo)
{
    // Lights that are named and not on disk: no header states a time either.
    auto scenario = built(ReportScenarioId::TwoThousandLights);
    ASSERT_TRUE(scenario.has_value());
    for (WorkflowManifest::RegistrationGroup& group : scenario->manifest.registrationGroups)
    {
        group.lightCaptureTimestampsUtc.clear();
    }

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportFrame& frame : model.masters.front().frames)
    {
        EXPECT_FALSE(frame.capturedAt.has_value());
        EXPECT_FALSE(frame.night.has_value());
    }
    EXPECT_TRUE(model.run.nights.empty());
    EXPECT_TRUE(namesNotStated(model, QStringLiteral("on which night")));
}

TEST(ReportFrameFacts, ALightsCaptureTimeIsItsHeadersWhenTheManifestStatesNone)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel stated = buildScenarioModel(*scenario);
    for (WorkflowManifest::RegistrationGroup& group : scenario->manifest.registrationGroups)
    {
        group.lightCaptureTimestampsUtc.clear();
    }

    const ReportModel fromHeaders = buildScenarioModel(*scenario);

    ASSERT_EQ(fromHeaders.masters.front().frames.size(), stated.masters.front().frames.size());
    for (std::size_t i = 0; i < stated.masters.front().frames.size(); ++i)
    {
        const ReportFrame& header = fromHeaders.masters.front().frames[i];
        const ReportFrame& manifest = stated.masters.front().frames[i];
        ASSERT_TRUE(header.capturedAt.has_value()) << header.fileName.toStdString();
        // A header states its start to the second.
        EXPECT_LT(std::abs(header.capturedAt->msecsTo(*manifest.capturedAt)), 1000);
        EXPECT_EQ(header.night, manifest.night);
    }
}

TEST(ReportFrameFacts, ALightStatesWhatItsHeaderStatesOfTheCamera)
{
    const auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportFrame& frame : model.masters.front().frames)
    {
        EXPECT_EQ(frame.gain, QStringLiteral("Gain 100"));
        EXPECT_EQ(frame.offset, QStringLiteral("Offset 50"));
        ASSERT_TRUE(frame.temperature.has_value());
        EXPECT_DOUBLE_EQ(*frame.temperature, -10.0);
    }
}

TEST(ReportFrameFacts, AShiftIsStatedForATransformWithAMatrixAndOnlyTheKindForAPolynomial)
{
    const auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const WorkflowManifest::RegistrationGroup& registration = scenario->manifest.registrationGroups.front();

    // The run's own records: a similarity per light, the reference moving nowhere.
    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster& master = model.masters.front();
    int moved = 0;
    for (const ReportFrame& frame : master.frames)
    {
        ASSERT_TRUE(frame.shiftX.has_value()) << frame.fileName.toStdString();
        ASSERT_TRUE(frame.shiftY.has_value());
        EXPECT_EQ(frame.transformKind, QStringLiteral("similarity"));
        moved += (std::abs(*frame.shiftX) > 1e-9 || std::abs(*frame.shiftY) > 1e-9) ? 1 : 0;
    }
    EXPECT_DOUBLE_EQ(*master.frames.front().shiftX, 0.0);
    EXPECT_EQ(moved, static_cast<int>(master.frames.size()) - 1);

    // Records of a polynomial: its matrix is not the whole transform, so no shift is read out of it.
    QList<QJsonObject> rows;
    for (std::size_t light = 0; light < registration.lightFiles.size(); ++light)
    {
        QJsonObject row;
        row.insert(QStringLiteral("frame_id"), registration.lightFiles[light].canonicalPath());
        row.insert(QStringLiteral("stars"), 400);
        row.insert(QStringLiteral("fwhm"), 2.5);
        row.insert(QStringLiteral("snr"), 30.0);
        row.insert(QStringLiteral("eccentricity"), 0.4);
        row.insert(QStringLiteral("succeeded"), true);
        row.insert(QStringLiteral("transform_json"),
                   light % 2u == 0u
                       ? QStringLiteral(R"({"kind":"polynomial3","matrix":[[1,0,5.5],[0,1,-7.25],[0,0,1]]})")
                       : QStringLiteral(R"({"kind":"affine","matrix":[[1,0,5.5],[0,1,-7.25],[0,0,1]]})"));
        rows.append(row);
    }
    const QString database = QDir(scenario->outputDirectory).filePath(QStringLiteral("polynomial_metrics.db"));
    ASSERT_TRUE(writeRegistrationMetricsDb(database, registration.groupKey.toString(), 3, rows));
    const ProductionReportSources production(scenario->session(), database);
    const ReportModel polynomial = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                      .result = scenario->result,
                                                                      .runDirectory = {},
                                                                      .context = scenario->context,
                                                                      .counters = std::nullopt},
                                                    production.sources());
    for (std::size_t light = 0; light < polynomial.masters.front().frames.size(); ++light)
    {
        const ReportFrame& frame = polynomial.masters.front().frames[light];
        if (light % 2u == 0u)
        {
            EXPECT_EQ(frame.transformKind, QStringLiteral("polynomial3"));
            EXPECT_FALSE(frame.shiftX.has_value());
            EXPECT_FALSE(frame.shiftY.has_value());
        }
        else
        {
            EXPECT_EQ(frame.transformKind, QStringLiteral("affine"));
            ASSERT_TRUE(frame.shiftX.has_value());
            EXPECT_DOUBLE_EQ(*frame.shiftX, 5.5);
            EXPECT_DOUBLE_EQ(*frame.shiftY, -7.25);
        }
    }
}

TEST(ReportFrameFacts, TheBackgroundNoiseOfALightIsStatedWhateverTheWeighting)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    int measured = 0;
    for (const ReportFrame& frame : model.masters.front().frames)
    {
        if (frame.state == ReportFrameState::ExcludedByUser)
        {
            // A light excluded in the lights' list was never given to the registration.
            EXPECT_FALSE(frame.backgroundNoise.has_value());
            continue;
        }
        // A light whose registration failed was still measured by the detection.
        ASSERT_TRUE(frame.backgroundNoise.has_value()) << frame.fileName.toStdString();
        EXPECT_GT(*frame.backgroundNoise, 0.0);
        ++measured;
    }
    EXPECT_GT(measured, 0);
}

TEST(ReportFrameFacts, TheNormalizationsScaleAndOffsetAreStatedLightByLight)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    const QString artifact =
        QDir(scenario->outputDirectory).filePath(QStringLiteral("normalization/one/coefficients.json"));
    ASSERT_TRUE(QDir().mkpath(QFileInfo(artifact).absolutePath()));
    group.normalizationCoefficientsArtifactPath = artifact;

    // One match per light, in an order of its own: the group's order reversed.
    ImageIntegration::NormalizationCoefficientSet set;
    set.mode = ImageIntegration::NormalizationCoefficientMode::Global;
    set.plane = ImageIntegration::NormalizationMeasurementPlane::RegisteredCanvas;
    set.channelCount = 1u;
    set.width = 64u;
    set.height = 48u;
    set.referenceIndex = 0u;
    for (std::size_t light = group.lightFiles.size(); light-- > 0u;)
    {
        set.logicalFrames.push_back(group.lightFiles[light].canonicalPath());
        set.sourceFrames.push_back(group.lightFiles[light].canonicalPath());
        an::Result<an::FrameNormalization> match = an::identityNormalization(
            an::NormalizationMethod::AdditiveScaling, 1u, an::ScaleEstimator::MedianAbsoluteDeviation);
        ASSERT_TRUE(match.hasValue());
        match->channels[0].scale = 1.0 + 0.01 * static_cast<double>(light);
        match->channels[0].offset = light == group.lightFiles.size() - 1u ? 0.0 : -0.001 * static_cast<double>(light);
        set.global.push_back(*match);
    }
    ASSERT_TRUE(ImageIntegration::checkNormalizationCoefficientSet(set).isEmpty())
        << ImageIntegration::checkNormalizationCoefficientSet(set).toStdString();
    const QString identity =
        ImageIntegration::normalizationArtifactIdentity(QStringLiteral("inputs"), QStringLiteral("settings"));
    const QJsonObject sidecar = ImageIntegration::normalizationSidecar(group.groupKey, QStringLiteral("inputs"),
                                                                       QStringLiteral("settings"), QString(), set);
    const auto published = ImageIntegration::publishNormalizationArtifact(artifact, set, identity, sidecar);
    ASSERT_TRUE(published.ok()) << published.message.toStdString();

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportMaster& master = model.masters.front();
    for (std::size_t light = 0; light < master.frames.size(); ++light)
    {
        const ReportFrame& frame = master.frames[light];
        ASSERT_EQ(frame.normalizationScale.size(), 1u) << frame.fileName.toStdString();
        ASSERT_EQ(frame.normalizationOffset.size(), 1u);
        EXPECT_DOUBLE_EQ(frame.normalizationScale.front(), 1.0 + 0.01 * static_cast<double>(light));
    }

    EXPECT_FALSE(namesNotStated(model, QStringLiteral("normalization")));
}

TEST(ReportFrameFacts, TheMastersPhotometryIsThatOfTheLightsItCombines)
{
    const auto scenario = built(ReportScenarioId::PhotometricAndLocalWeighting);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportMaster& master : model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        ASSERT_TRUE(master.photometry.has_value());
        EXPECT_EQ(master.photometry->frames, static_cast<int>(master.frames.size()));
        ASSERT_TRUE(master.photometry->medianZeroPoint.has_value());
        EXPECT_NEAR(*master.photometry->medianZeroPoint, 24.62, 0.13);
        ASSERT_TRUE(master.photometry->medianLimitingMagnitude.has_value());
        EXPECT_NEAR(*master.photometry->medianLimitingMagnitude, 17.8, 0.31);
        EXPECT_TRUE(master.photometry->medianZeroPointScatter.has_value());
        EXPECT_TRUE(master.photometry->medianCatalogMatches.has_value());
    }

    // Lights the registration matched against no catalog state no photometry.
    const auto plain = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(buildScenarioModel(*plain).masters.front().photometry.has_value());
}

TEST(ReportFrameFacts, TheFramesOfTwoThousandLightsBuildInUnderASecond)
{
    const auto scenario = built(ReportScenarioId::TwoThousandLights);
    ASSERT_TRUE(scenario.has_value());

    QElapsedTimer timer;
    timer.start();
    const ReportModel model = buildScenarioModel(*scenario);
    const qint64 elapsedMs = timer.elapsed();

    ASSERT_EQ(model.masters.front().frames.size(), 2000u);
    EXPECT_EQ(framesOfState(model, ReportFrameState::RegistrationFailed), 40);
    EXPECT_EQ(framesOfState(model, ReportFrameState::Used), 1960);
#ifdef NDEBUG
    // The whole model of the run, its 2,000 lights among it: a wall-clock bound is a Release figure.
    EXPECT_LT(elapsedMs, 1000) << "the model of 2,000 lights took " << elapsedMs << " ms";
#else
    (void)elapsedMs;
#endif
}
