// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StandaloneRegistrationUtils.h
 * @brief Shared helpers for standalone registration behavior.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/ImageIntegration/persistence/RegistrationMetricsRepository.h"

#include <QString>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace an::ui
{
/**
 * @brief User-configurable quality thresholds for standalone registration frames.
 */
struct StandaloneRegistrationQualityThresholds
{
    int minStars = 0;
    double maxFwhm = 0.0;
    double maxEccentricity = 0.0;

    [[nodiscard]] bool isActive() const noexcept
    {
        return minStars > 0 || maxFwhm > 0.0 || maxEccentricity > 0.0;
    }
};

/**
 * @brief One frame of the standalone registration's list, as the helpers read it: its file and its filter.
 */
struct StandaloneRegistrationFrame
{
    QString path;
    QString filter;
};

enum class StandaloneRegistrationExclusionReason
{
    None,
    Manual,
    QualityThreshold
};

struct StandaloneRegistrationExclusion
{
    bool excluded = false;
    StandaloneRegistrationExclusionReason reason = StandaloneRegistrationExclusionReason::None;
    QString detail;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool
standaloneRegistrationMetricPassesThresholds(const RegistrationMetricRecord& metric,
                                             const StandaloneRegistrationQualityThresholds& thresholds);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::unordered_set<QString> computeStandaloneThresholdExcludedFrameIds(
    const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
    const std::vector<StandaloneRegistrationFrame>& images, const StandaloneRegistrationQualityThresholds& thresholds);

[[nodiscard]] AFTERNIGHTPROCESSES_API StandaloneRegistrationExclusion
evaluateStandaloneFrameExclusion(const QString& frameId, const std::unordered_set<QString>& manualExcludedFrameIds,
                                 const std::unordered_set<QString>& thresholdExcludedFrameIds);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<AutoReferencePickResult>
pickStandaloneAutoReference(const std::vector<StandaloneRegistrationFrame>& images,
                            const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
                            QString& errorOut);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<AutoReferencePickResult>
pickStandaloneAutoReference(const std::vector<StandaloneRegistrationFrame>& images,
                            const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
                            const std::unordered_set<QString>& ignoredFrameIds, QString& errorOut);
} // namespace an::ui
