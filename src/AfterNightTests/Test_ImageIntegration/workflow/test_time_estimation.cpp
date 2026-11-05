// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

TEST(WorkflowPreparationTiming, KeepsExclusiveDurationsDistinctAndDerivesTotal)
{
    WorkflowPreparationTiming timing;
    timing.captured = true;
    timing.manifestBuildMs = 137;
    timing.planAndValidationMs = 263;

    EXPECT_EQ(timing.manifestBuildMs, 137);
    EXPECT_EQ(timing.planAndValidationMs, 263);
    EXPECT_EQ(timing.totalMs(), 400);
    EXPECT_TRUE(timing.hasMeasurements());
}

TEST(WorkflowPreparationTiming, DefaultsToZeroForCallersWithoutPreparationData)
{
    const WorkflowPreparationTiming timing;

    EXPECT_EQ(timing.manifestBuildMs, 0);
    EXPECT_EQ(timing.planAndValidationMs, 0);
    EXPECT_EQ(timing.totalMs(), 0);
    EXPECT_FALSE(timing.hasMeasurements());
}

TEST(WorkflowTimeEstimator, EstimatesEmptyManifest)
{
    WorkflowManifest manifest;
    EXPECT_EQ(WorkflowTimeEstimator::estimateTotalSeconds(manifest), 0);
}

TEST(WorkflowTimeEstimator, EstimatesBiasIntegration)
{
    WorkflowManifest manifest;
    WorkflowManifest::CalibrationStage stage;
    WorkflowManifest::CalibrationStage::Group group;
    // Add 100 frames
    for (int i = 0; i < 100; ++i)
        group.inputFiles.emplace_back("f.fit");
    stage.groups.push_back(group);
    manifest.biasStage = stage;

    // 100 * 0.05 = 5s
    EXPECT_EQ(WorkflowTimeEstimator::estimateTotalSeconds(manifest), 5);
}

TEST(WorkflowTimeEstimator, RegistrationEstimateIsDominatedByStarDetection)
{
    WorkflowManifest manifest;
    WorkflowManifest::RegistrationGroup group;
    for (int i = 0; i < 100; ++i)
        group.lightFiles.emplace_back(QStringLiteral("light.fit"));
    manifest.registrationGroups.push_back(group);

    // 100 * (1.0 detection + 0.05 transforms) = 105s. Star detection is ~95% of the stage on
    // real runs, so an estimate that only models transform matching is off by an order of
    // magnitude.
    EXPECT_EQ(WorkflowTimeEstimator::estimateTotalSeconds(manifest), 105);
}

TEST(WorkflowTimeEstimator, FormatsDurationCorrectly)
{
    EXPECT_EQ(WorkflowTimeEstimator::formatDuration(45), "45s");
    EXPECT_EQ(WorkflowTimeEstimator::formatDuration(125), "2m 5s");
    EXPECT_EQ(WorkflowTimeEstimator::formatDuration(3665), "1h 1m");
}
