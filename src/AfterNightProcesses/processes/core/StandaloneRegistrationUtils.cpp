// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StandaloneRegistrationUtils.h"

#include "processes/ImageIntegration/model/FrameId.h"

#include <QHash>

namespace an::ui
{
bool standaloneRegistrationMetricPassesThresholds(const RegistrationMetricRecord& metric,
                                                  const StandaloneRegistrationQualityThresholds& thresholds)
{
    if (!metric.metrics.succeeded)
    {
        return false;
    }

    if (thresholds.minStars > 0 && metric.metrics.stars < thresholds.minStars)
    {
        return false;
    }
    if (thresholds.maxFwhm > 0.0 && metric.metrics.fwhm > thresholds.maxFwhm)
    {
        return false;
    }
    if (thresholds.maxEccentricity > 0.0 && metric.metrics.eccentricity > thresholds.maxEccentricity)
    {
        return false;
    }

    return true;
}

std::unordered_set<QString> computeStandaloneThresholdExcludedFrameIds(
    const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
    const std::vector<StandaloneRegistrationFrame>& images, const StandaloneRegistrationQualityThresholds& thresholds)
{
    std::unordered_set<QString> excludedFrameIds;
    if (!thresholds.isActive())
    {
        return excludedFrameIds;
    }

    excludedFrameIds.reserve(images.size());
    for (const StandaloneRegistrationFrame& image : images)
    {
        if (image.path.isEmpty())
        {
            continue;
        }

        const QString frameId = registrationCanonicalFrameKey(image.path);
        if (frameId.isEmpty())
        {
            continue;
        }

        const auto metricIt = metricsByFrameId.find(frameId);
        if (metricIt == metricsByFrameId.end())
        {
            continue;
        }

        if (!standaloneRegistrationMetricPassesThresholds(metricIt->second, thresholds))
        {
            excludedFrameIds.insert(frameId);
        }
    }

    return excludedFrameIds;
}

StandaloneRegistrationExclusion
evaluateStandaloneFrameExclusion(const QString& frameId, const std::unordered_set<QString>& manualExcludedFrameIds,
                                 const std::unordered_set<QString>& thresholdExcludedFrameIds)
{
    const QString canonical = registrationCanonicalFrameKey(frameId);
    if (canonical.isEmpty())
    {
        return {};
    }

    if (manualExcludedFrameIds.contains(canonical))
    {
        return {true, StandaloneRegistrationExclusionReason::Manual, QStringLiteral("Excluded manually")};
    }
    if (thresholdExcludedFrameIds.contains(canonical))
    {
        return {true, StandaloneRegistrationExclusionReason::QualityThreshold,
                QStringLiteral("Excluded by quality thresholds")};
    }

    return {};
}

std::optional<AutoReferencePickResult>
pickStandaloneAutoReference(const std::vector<StandaloneRegistrationFrame>& images,
                            const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
                            QString& errorOut)
{
    return pickStandaloneAutoReference(images, metricsByFrameId, std::unordered_set<QString>{}, errorOut);
}

std::optional<AutoReferencePickResult>
pickStandaloneAutoReference(const std::vector<StandaloneRegistrationFrame>& images,
                            const std::unordered_map<QString, RegistrationMetricRecord>& metricsByFrameId,
                            const std::unordered_set<QString>& ignoredFrameIds, QString& errorOut)
{
    errorOut.clear();

    QHash<QString, QString> filterByFrameId;
    filterByFrameId.reserve(static_cast<int>(images.size()));
    std::vector<RegistrationMetricsCandidate> candidates;
    candidates.reserve(images.size());

    for (const StandaloneRegistrationFrame& image : images)
    {
        if (image.path.isEmpty())
        {
            continue;
        }

        const QString frameId = registrationCanonicalFrameKey(image.path);
        if (frameId.isEmpty())
        {
            continue;
        }

        if (ignoredFrameIds.contains(frameId))
        {
            continue;
        }

        const auto metricIt = metricsByFrameId.find(frameId);
        if (metricIt == metricsByFrameId.end() || !metricIt->second.metrics.succeeded)
        {
            continue;
        }

        candidates.push_back(RegistrationMetricsCandidate{FrameId(frameId), metricIt->second.metrics});
        filterByFrameId.insert(frameId, image.filter.trimmed());
    }

    const auto getFilter = [&](const FrameId& id) -> QString {
        const QString filter = filterByFrameId.value(id.canonicalPath()).trimmed();
        return filter.isEmpty() ? QStringLiteral("(unknown)") : filter;
    };

    return RegistrationReferenceScorer::pickBest(QStringLiteral("standalone"), candidates, getFilter, errorOut);
}
} // namespace an::ui
