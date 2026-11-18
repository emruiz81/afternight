// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowTimeEstimator.h"

#include <QTime>

double WorkflowTimeEstimator::StageSeconds::of(const QString& stageToken) const
{
    if (stageToken == QLatin1String("Bias"))
    {
        return bias;
    }
    if (stageToken == QLatin1String("Dark"))
    {
        return dark;
    }
    if (stageToken == QLatin1String("Flat"))
    {
        return flat;
    }
    if (stageToken == QLatin1String("LightsCalibration"))
    {
        return lightsCalibration;
    }
    if (stageToken == QLatin1String("LightsRegistration"))
    {
        return lightsRegistration;
    }
    if (stageToken == QLatin1String("LightsNormalization"))
    {
        return lightsNormalization;
    }
    if (stageToken == QLatin1String("LightsDrizzle"))
    {
        return lightsDrizzle;
    }
    if (stageToken == QLatin1String("LightsStacking"))
    {
        return lightsStacking;
    }
    return 0.0;
}

double WorkflowTimeEstimator::StageSeconds::total() const
{
    return bias + dark + flat + lightsCalibration + lightsRegistration + lightsNormalization + lightsDrizzle +
           lightsStacking;
}

WorkflowTimeEstimator::StageSeconds WorkflowTimeEstimator::estimateStageSeconds(const WorkflowManifest& manifest)
{
    StageSeconds seconds;

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
                seconds.bias += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (const auto& group : manifest.darkStage->groups)
        {
            if (!group.skipIfExists)
                seconds.dark += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            if (!group.skipIfExists)
                seconds.flat += group.inputFiles.size() * kIntegrationTimePerFrame;
        }
    }

    // 2. Lights Calibration
    // manifest.lightsCalibration contains all light frames to be calibrated
    seconds.lightsCalibration += manifest.lightsCalibration.size() * kCalibrationTimePerFrame;

    // 3. Registration (star detection + transform computation)
    for (const auto& group : manifest.registrationGroups)
    {
        seconds.lightsRegistration +=
            group.lightFiles.size() * (kStarDetectionTimePerFrame + kRegistrationTransformTimePerFrame);
    }

    // 4. Integration (Normalization + Stacking + Drizzle)
    for (const auto& group : manifest.integrationGroups)
    {
        if (group.skipIfExists)
            continue;

        seconds.lightsStacking += group.lightFiles.size() * kStackingTimePerFrame;
        if (!group.normalizationReferenceFrames.empty())
        {
            seconds.lightsNormalization += group.lightFiles.size() * kNormalizationTimePerFrame;
        }
        if (group.drizzle.has_value() && group.drizzle->enabled)
        {
            seconds.lightsDrizzle += group.lightFiles.size() * kDrizzleTimePerFrame;
        }
    }

    return seconds;
}

int WorkflowTimeEstimator::estimateTotalSeconds(const WorkflowManifest& manifest)
{
    return static_cast<int>(estimateStageSeconds(manifest).total());
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
