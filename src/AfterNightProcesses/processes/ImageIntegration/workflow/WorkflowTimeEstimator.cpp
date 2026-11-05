// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowTimeEstimator.h"

#include <QTime>

int WorkflowTimeEstimator::estimateTotalSeconds(const WorkflowManifest& manifest)
{
    double totalSeconds = 0.0;

    // Heuristics (seconds per frame)
    constexpr double kIntegrationTimePerFrame = 0.05;
    constexpr double kCalibrationTimePerFrame = 0.1;
    // Registration is two very unequal phases: star detection dominates (it profiles every
    // frame) while transform computation only matches star lists. Modelling them separately
    // keeps the estimate from being built on the cheaper of the two.
    constexpr double kStarDetectionTimePerFrame = 1.0;
    constexpr double kRegistrationTransformTimePerFrame = 0.05;
    constexpr double kNormalizationTimePerFrame = 0.2;
    constexpr double kStackingTimePerFrame = 0.3;
    constexpr double kDrizzleTimePerFrame = 0.2;

    // 1. Master Generation
    if (manifest.biasStage.has_value())
    {
        for (const auto& group : manifest.biasStage->groups)
        {
            if (!group.skipIfExists)
                totalSeconds += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (const auto& group : manifest.darkStage->groups)
        {
            if (!group.skipIfExists)
                totalSeconds += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            if (!group.skipIfExists)
                totalSeconds += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }

    // 2. Lights Calibration
    // manifest.lightsCalibration contains all light frames to be calibrated
    totalSeconds += manifest.lightsCalibration.size() * kCalibrationTimePerFrame;

    // 3. Registration (star detection + transform computation)
    for (const auto& group : manifest.registrationGroups)
    {
        totalSeconds += group.lightFiles.size() * (kStarDetectionTimePerFrame + kRegistrationTransformTimePerFrame);
    }

    // 4. Integration (Normalization + Stacking + Drizzle)
    for (const auto& group : manifest.integrationGroups)
    {
        if (group.skipIfExists)
            continue;

        double groupCost = kStackingTimePerFrame;
        if (!group.normalizationReferenceFrames.empty())
        {
            groupCost += kNormalizationTimePerFrame;
        }
        if (group.drizzle.has_value() && group.drizzle->enabled)
        {
            groupCost += kDrizzleTimePerFrame;
        }

        totalSeconds += group.lightFiles.size() * groupCost;
    }

    return static_cast<int>(totalSeconds);
}

QString WorkflowTimeEstimator::formatDuration(int seconds)
{
    if (seconds < 60)
    {
        return QStringLiteral("%1s").arg(seconds);
    }
    if (seconds < 3600)
    {
        int min = seconds / 60;
        int sec = seconds % 60;
        return QStringLiteral("%1m %2s").arg(min).arg(sec);
    }
    int hr = seconds / 3600;
    int min = (seconds % 3600) / 60;
    return QStringLiteral("%1h %2m").arg(hr).arg(min);
}
