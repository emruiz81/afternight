// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StandaloneStackingSettingsPersistence.h
 * @brief Shared helpers for standalone stacking settings persistence/state sync.
 */
#pragma once

#include "processes/core/WorkflowSettingsRecords.h"

#include <nlohmann/json.hpp>

namespace an::ui
{
[[nodiscard]] inline LocalNormalizationSettings standaloneStackingDefaultLocalNormalizationSettings()
{
    return {};
}

inline void resetStandaloneStackingLocalNormalizationSettings(LocalNormalizationSettings& settings)
{
    settings = standaloneStackingDefaultLocalNormalizationSettings();
}

inline void synchronizeStandaloneStackingLocalNormalization(StackingSettings& settings,
                                                            const LocalNormalizationSettings& sharedSettings)
{
    if (settings.normalizationMode != NormalizationMode::Local)
    {
        settings.localNormalization.enabled = false;
        return;
    }

    settings.localNormalization = sharedSettings;
    settings.localNormalization.enabled = true;
}

inline void serializeStandaloneStackingLocalNormalization(nlohmann::json& params,
                                                          const LocalNormalizationSettings& settings)
{
    nlohmann::json localNormalization;
    localNormalization["gridSize"] = settings.gridSize;
    localNormalization["gridInterpolation"] = settings.gridInterpolation.toStdString();
    localNormalization["localScaleEnabled"] = settings.localScaleEnabled;
    localNormalization["backgroundEstimator"] = settings.backgroundEstimator.toStdString();
    localNormalization["backgroundPercentile"] = settings.backgroundPercentile;
    localNormalization["sigmaClipK"] = settings.sigmaClipK;
    localNormalization["sigmaClipIterations"] = settings.sigmaClipIterations;
    localNormalization["backgroundSmoothingRadius"] = settings.backgroundSmoothingRadius;
    localNormalization["scaleMode"] = settings.scaleMode.toStdString();
    localNormalization["lowClip"] = settings.lowClip;
    localNormalization["highClip"] = settings.highClip;
    localNormalization["localScaleSmoothingRadius"] = settings.localScaleSmoothingRadius;
    params["localNormalization"] = std::move(localNormalization);
}

[[nodiscard]] inline LocalNormalizationSettings
deserializeStandaloneStackingLocalNormalization(const nlohmann::json& params)
{
    LocalNormalizationSettings settings = standaloneStackingDefaultLocalNormalizationSettings();
    if (!params.contains("localNormalization"))
    {
        return settings;
    }

    const auto& localNormalization = params.at("localNormalization");
    if (!localNormalization.is_object())
    {
        return settings;
    }

    settings.gridSize = localNormalization.value("gridSize", settings.gridSize);
    settings.gridInterpolation =
        QString::fromStdString(localNormalization.value("gridInterpolation", settings.gridInterpolation.toStdString()));
    settings.localScaleEnabled = localNormalization.value("localScaleEnabled", settings.localScaleEnabled);
    settings.backgroundEstimator = QString::fromStdString(
        localNormalization.value("backgroundEstimator", settings.backgroundEstimator.toStdString()));
    settings.backgroundPercentile = localNormalization.value("backgroundPercentile", settings.backgroundPercentile);
    settings.sigmaClipK = localNormalization.value("sigmaClipK", settings.sigmaClipK);
    settings.sigmaClipIterations = localNormalization.value("sigmaClipIterations", settings.sigmaClipIterations);
    settings.backgroundSmoothingRadius =
        localNormalization.value("backgroundSmoothingRadius", settings.backgroundSmoothingRadius);
    settings.scaleMode =
        QString::fromStdString(localNormalization.value("scaleMode", settings.scaleMode.toStdString()));
    settings.lowClip = localNormalization.value("lowClip", settings.lowClip);
    settings.highClip = localNormalization.value("highClip", settings.highClip);
    settings.localScaleSmoothingRadius =
        localNormalization.value("localScaleSmoothingRadius", settings.localScaleSmoothingRadius);
    return settings;
}
} // namespace an::ui
