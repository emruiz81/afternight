// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where a run stands among its stages: the step it is at, counted among the stages it runs, the stage's name, how far
/// the stage has come and how far the whole - each stage weighing what the run is expected to spend in it, or the same
/// as the others when nothing says - and the time left, told once enough of the run has passed to tell.

#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{

using ImageIntegration::workflowRunStanding;
using ImageIntegration::WorkflowRunStanding;
using ImageIntegration::workflowRunStandingText;

const QStringList kNine = {QStringLiteral("Bias"),
                           QStringLiteral("Dark"),
                           QStringLiteral("Flat"),
                           QStringLiteral("LightsCalibration"),
                           QStringLiteral("LightsRegistration"),
                           QStringLiteral("LightsAlignment"),
                           QStringLiteral("LightsNormalization"),
                           QStringLiteral("LightsDrizzle"),
                           QStringLiteral("LightsStacking")};

WorkflowProgress at(const WorkflowStage stage, const float stageFraction)
{
    WorkflowProgress progress;
    progress.currentStage = stage;
    progress.stageProgress = stageFraction;
    progress.overallProgress = stageFraction;
    return progress;
}

} // namespace

// The step is the stage's place among the stages the run goes through, and the text names both.
TEST(WorkflowRunStanding, SaysTheStepAmongTheStagesTheRunGoesThrough)
{
    const WorkflowRunStanding standing = workflowRunStanding(kNine, {}, at(WorkflowStage::LightsRegistration, 0.5f));
    EXPECT_EQ(standing.step, 5);
    EXPECT_EQ(standing.steps, 9);
    EXPECT_EQ(standing.stage, QStringLiteral("Registration"));
    EXPECT_DOUBLE_EQ(standing.stageFraction, 0.5);
    EXPECT_EQ(workflowRunStandingText(standing), QStringLiteral("Step 5/9: Registration"));

    const QStringList three = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsRegistration"),
                               QStringLiteral("LightsStacking")};
    const WorkflowRunStanding short3 = workflowRunStanding(three, {}, at(WorkflowStage::LightsCalibration, 0.0f));
    EXPECT_EQ(workflowRunStandingText(short3), QStringLiteral("Step 1/3: Lights calibration"));
    EXPECT_EQ(workflowRunStandingText(workflowRunStanding(three, {}, at(WorkflowStage::LightsStacking, 1.0f))),
              QStringLiteral("Step 3/3: Stacking"));
}

// With nothing said of what each stage costs, the stages weigh alike: the whole's fraction is the stages passed and
// the share of the one on its way.
TEST(WorkflowRunStanding, TheWholesFractionCountsTheStagesAlikeWithoutWeights)
{
    EXPECT_NEAR(workflowRunStanding(kNine, {}, at(WorkflowStage::LightsRegistration, 0.5f)).fraction, 4.5 / 9.0, 1e-9);
    EXPECT_NEAR(workflowRunStanding(kNine, {}, at(WorkflowStage::Bias, 0.0f)).fraction, 0.0, 1e-9);
    EXPECT_NEAR(workflowRunStanding(kNine, {}, at(WorkflowStage::LightsStacking, 1.0f)).fraction, 1.0, 1e-9);
    // A fraction a stage reports out of its bounds is held to them.
    EXPECT_NEAR(workflowRunStanding(kNine, {}, at(WorkflowStage::Bias, 7.0f)).fraction, 1.0 / 9.0, 1e-9);
}

// A weight a stage says what share of the run it is: a run that spends nine tenths in its registration is half done
// well into it, not at its start.
TEST(WorkflowRunStanding, AStageWeighsWhatTheRunSpendsInIt)
{
    const QStringList three = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsRegistration"),
                               QStringLiteral("LightsStacking")};
    const std::vector<double> weights = {5.0, 90.0, 5.0};
    EXPECT_NEAR(workflowRunStanding(three, weights, at(WorkflowStage::LightsRegistration, 0.0f)).fraction, 0.05, 1e-9);
    EXPECT_NEAR(workflowRunStanding(three, weights, at(WorkflowStage::LightsRegistration, 0.5f)).fraction, 0.5, 1e-9);
    EXPECT_NEAR(workflowRunStanding(three, weights, at(WorkflowStage::LightsStacking, 0.0f)).fraction, 0.95, 1e-9);
    // Weights that do not go with the stages - too few, or none above nothing - are not used.
    EXPECT_NEAR(workflowRunStanding(three, {1.0}, at(WorkflowStage::LightsRegistration, 0.5f)).fraction, 1.5 / 3.0,
                1e-9);
    EXPECT_NEAR(workflowRunStanding(three, {0.0, 0.0, 0.0}, at(WorkflowStage::LightsRegistration, 0.5f)).fraction,
                1.5 / 3.0, 1e-9);
}

// A run that names no stage it is at - it prepares - and a stage the run's sequence does not name have no step: the
// fraction is the stage's own, and the text is empty.
TEST(WorkflowRunStanding, ARunAtNoKnownStageHasNoStep)
{
    const WorkflowRunStanding none = workflowRunStanding(kNine, {}, at(WorkflowStage::None, 0.3f));
    EXPECT_EQ(none.step, 0);
    EXPECT_TRUE(workflowRunStandingText(none).isEmpty());

    const QStringList preparing = {QStringLiteral("Preparing")};
    const WorkflowRunStanding foreign =
        workflowRunStanding(preparing, {}, at(WorkflowStage::LightsRegistration, 0.42f));
    EXPECT_EQ(foreign.step, 0);
    EXPECT_EQ(foreign.steps, 1);
    EXPECT_NEAR(foreign.fraction, 0.42, 1e-6);
    EXPECT_EQ(foreign.stage, QStringLiteral("Registration"));

    const WorkflowRunStanding bare = workflowRunStanding({}, {}, at(WorkflowStage::LightsStacking, 0.25f));
    EXPECT_EQ(bare.step, 0);
    EXPECT_NEAR(bare.fraction, 0.25, 1e-6);
}

// The stages' weights are read from the manifest: what the run is expected to spend in each stage it names, none of
// them nothing.
TEST(WorkflowRunStanding, WeightsComeFromWhatTheManifestAsksOfEachStage)
{
    WorkflowManifest manifest;
    manifest.stageSequence = QStringList{QStringLiteral("LightsCalibration"), QStringLiteral("LightsRegistration"),
                                         QStringLiteral("LightsStacking")};
    WorkflowManifest::RegistrationGroup registration;
    for (int light = 0; light < 100; ++light)
    {
        registration.lightFiles.push_back(FrameId(QStringLiteral("C:/lights/light_%1.fit").arg(light)));
    }
    manifest.registrationGroups.push_back(registration);
    const std::vector<double> weights = ImageIntegration::workflowStageWeights(manifest);
    ASSERT_EQ(weights.size(), 3u);
    EXPECT_GT(weights[0], 0.0) << "a stage with nothing counted still weighs something";
    EXPECT_GT(weights[2], 0.0);
    EXPECT_GT(weights[1], 10.0 * weights[0]) << "a hundred lights to register outweigh a calibration of none";
}

// The time left is the time passed scaled by what is left to do, told only once the run has gone far and long enough
// for the figure to mean something, and never for a run that is done.
TEST(WorkflowRunStanding, TheTimeLeftIsToldOnceTheRunCanTell)
{
    using ImageIntegration::workflowRemainingMs;
    EXPECT_FALSE(workflowRemainingMs(2'000, 0.5).has_value()) << "too soon";
    EXPECT_FALSE(workflowRemainingMs(60'000, 0.01).has_value()) << "too little done";
    EXPECT_FALSE(workflowRemainingMs(60'000, 1.0).has_value()) << "nothing left";
    const std::optional<qint64> half = workflowRemainingMs(60'000, 0.5);
    ASSERT_TRUE(half.has_value());
    EXPECT_EQ(*half, 60'000);
    const std::optional<qint64> quarter = workflowRemainingMs(100'000, 0.25);
    ASSERT_TRUE(quarter.has_value());
    EXPECT_EQ(*quarter, 300'000);
}
