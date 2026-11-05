// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationMatcher.h"
#include "FrameId.h"
#include "processes/ImageIntegration/core/MathUtils.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <limits>

using ImageIntegration::clamp01;

namespace
{
constexpr double kTempAmbiguityEpsilonC = 0.25;
constexpr double kExposureAmbiguityEpsilonSeconds = 1e-6;

double temperatureDistance(std::optional<double> a, std::optional<double> b)
{
    if (!a.has_value() || !b.has_value())
    {
        return std::numeric_limits<double>::infinity();
    }
    return std::abs(a.value() - b.value());
}

bool lightKeysEqual(const LightGroupKey& lhs, const LightGroupKey& rhs)
{
    return lhs.resolution == rhs.resolution && lhs.isoGain == rhs.isoGain && lhs.offset == rhs.offset &&
           lhs.exposureSeconds == rhs.exposureSeconds && lhs.filter == rhs.filter && lhs.objectName == rhs.objectName &&
           lhs.session == rhs.session && lhs.customTag == rhs.customTag;
}

bool matchesExceptSession(const FlatGroupKey& flatKey, const LightGroupKey& lightKey, const GroupingOptions& options)
{
    if (flatKey.resolution != lightKey.resolution)
    {
        return false;
    }

    if (options.byIsoGain && flatKey.isoGain != lightKey.isoGain)
    {
        return false;
    }

    if (options.byIsoGain && options.byOffset && flatKey.offset != lightKey.offset)
    {
        return false;
    }

    if (options.byFilter && flatKey.filter != lightKey.filter)
    {
        return false;
    }

    if (options.byObject)
    {
        if (!flatKey.objectName.isEmpty() && flatKey.objectName != QStringLiteral("-") &&
            flatKey.objectName != lightKey.objectName)
        {
            return false;
        }
    }

    if (options.byCustomTag && flatKey.customTag != lightKey.customTag)
    {
        return false;
    }

    return true;
}

/// The group a master assigned by hand belongs to: the candidate whose master is the same file, as a master chosen
/// from a panel's candidates is, or, for a master no candidate names - one browsed to - the key the override was
/// stored with. Deciding which groups a run stacks, or which group a match points at, reads this key.
template <typename CalibrationGroup>
QString assignedGroupKey(const std::vector<CalibrationGroup>& groups, const CalibrationOverride& assigned)
{
    if (!assigned.masterPath.trimmed().isEmpty())
    {
        const FrameId master(assigned.masterPath);
        for (const auto& group : groups)
        {
            if (!group.masterPath.trimmed().isEmpty() && FrameId(group.masterPath) == master)
            {
                return group.groupKey.toString();
            }
        }
    }
    return assigned.groupKey.toString();
}

CalibrationMatch makeFallbackWarning(CalibrationMatch match, const CalibrationMatchingPolicy& policy,
                                     const QString& message)
{
    if (policy.warnOnFallback)
    {
        match.warningMessage = message;
    }

    return match;
}
} // namespace

CalibrationMatcher::CalibrationMatcher(GroupingOptions biasOptions, GroupingOptions darkOptions,
                                       GroupingOptions flatOptions, CalibrationMatchingPolicy policy)
    : m_biasOptions(biasOptions), m_darkOptions(darkOptions), m_flatOptions(flatOptions), m_policy(policy)
{
}

void CalibrationMatcher::setBiasGroups(std::vector<BiasCalibrationGroup> groups)
{
    std::sort(groups.begin(), groups.end(), [](const BiasCalibrationGroup& lhs, const BiasCalibrationGroup& rhs) {
        return lhs.groupKey < rhs.groupKey;
    });
    m_biasGroups = std::move(groups);
}

void CalibrationMatcher::setDarkGroups(std::vector<DarkCalibrationGroup> groups)
{
    std::sort(groups.begin(), groups.end(), [](const DarkCalibrationGroup& lhs, const DarkCalibrationGroup& rhs) {
        return lhs.groupKey < rhs.groupKey;
    });
    m_darkGroups = std::move(groups);
}

void CalibrationMatcher::setFlatGroups(std::vector<FlatCalibrationGroup> groups)
{
    std::sort(groups.begin(), groups.end(), [](const FlatCalibrationGroup& lhs, const FlatCalibrationGroup& rhs) {
        return lhs.groupKey < rhs.groupKey;
    });
    m_flatGroups = std::move(groups);
}

void CalibrationMatcher::setOverrides(std::vector<CalibrationOverride> overrides)
{
    m_overrides = std::move(overrides);
}

CalibrationMatch CalibrationMatcher::findBias(const LightGroupKey& lightKey) const
{
    if (auto override_match = findOverride(lightKey, CalibrationOverrideType::Bias))
    {
        // Check for unassigned marker
        if (override_match->masterPath == QString::fromLatin1(kUnassignedCalibrationMarker))
        {
            CalibrationMatch match;
            match.type = CalibrationMatch::MatchType::ManuallyUnassigned;
            match.confidence = 1.0;
            return match;
        }

        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::ManualOverride;
        match.masterPath = override_match->masterPath;
        match.matchedGroupKey = assignedGroupKey(m_biasGroups, *override_match);
        match.confidence = 1.0;
        return match;
    }

    CalibrationGroupKey calibration = CalibrationGroupKey::fromLightKey(lightKey);
    int matches = 0;
    BiasCalibrationGroup best_group;
    for (const auto& group : m_biasGroups)
    {
        if (calibration.matchesBias(group.key, m_biasOptions))
        {
            if (matches == 0 || group.groupKey < best_group.groupKey)
            {
                best_group = group;
            }
            ++matches;
        }
    }

    if (matches > 0)
    {
        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::Exact;
        match.masterPath = best_group.masterPath;
        match.matchedGroupKey = best_group.groupKey.toString();
        match.needsUserAttention = (matches > 1);
        match.confidence = match.needsUserAttention ? 0.8 : 1.0;
        if (match.needsUserAttention && m_policy.warnOnFallback)
        {
            match.warningMessage = QStringLiteral("Multiple BIAS matches found; using deterministic selection.");
        }
        return match;
    }

    return {};
}

CalibrationMatch CalibrationMatcher::findDark(const LightGroupKey& lightKey) const
{
    return findDark(lightKey, std::nullopt);
}

CalibrationMatch CalibrationMatcher::findDark(const LightGroupKey& lightKey,
                                              std::optional<double> lightTemperatureC) const
{
    if (auto override_match = findOverride(lightKey, CalibrationOverrideType::Dark))
    {
        // Check for unassigned marker
        if (override_match->masterPath == QString::fromLatin1(kUnassignedCalibrationMarker))
        {
            CalibrationMatch match;
            match.type = CalibrationMatch::MatchType::ManuallyUnassigned;
            match.confidence = 1.0;
            return match;
        }

        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::ManualOverride;
        match.masterPath = override_match->masterPath;
        match.matchedGroupKey = assignedGroupKey(m_darkGroups, *override_match);
        match.confidence = 1.0;
        return match;
    }

    CalibrationGroupKey calibration = CalibrationGroupKey::fromLightKey(lightKey);

    const DarkCalibrationGroup* best_group = nullptr;
    double best_temp_diff = std::numeric_limits<double>::infinity();
    int matches = 0;

    for (const auto& group : m_darkGroups)
    {
        if (calibration.matchesDark(group.key, m_darkOptions))
        {
            ++matches;
            const double tdiff = temperatureDistance(group.key.temperatureC, lightTemperatureC);
            const bool has_temp = std::isfinite(tdiff);

            if (!best_group)
            {
                best_group = &group;
                best_temp_diff = tdiff;
                continue;
            }

            // Prefer closest temperature when available; otherwise deterministic GroupKey ordering.
            const double best_has_temp = std::isfinite(best_temp_diff);
            if (has_temp && (!best_has_temp || tdiff < best_temp_diff - kTempAmbiguityEpsilonC))
            {
                best_group = &group;
                best_temp_diff = tdiff;
            }
            else if ((!has_temp && !best_has_temp) && group.groupKey < best_group->groupKey)
            {
                best_group = &group;
                best_temp_diff = tdiff;
            }
            else if (has_temp && best_has_temp && std::abs(tdiff - best_temp_diff) <= kTempAmbiguityEpsilonC &&
                     group.groupKey < best_group->groupKey)
            {
                best_group = &group;
                best_temp_diff = tdiff;
            }
        }
    }

    if (best_group)
    {
        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::Exact;
        match.masterPath = best_group->masterPath;
        match.matchedGroupKey = best_group->groupKey.toString();

        // If temperature is provided and there are multiple candidates, mark ambiguous when
        // the best temperature match isn't clearly better than others.
        if (lightTemperatureC.has_value() && matches > 1)
        {
            int near_best = 0;
            for (const auto& group : m_darkGroups)
            {
                if (!calibration.matchesDark(group.key, m_darkOptions))
                {
                    continue;
                }
                const double tdiff = temperatureDistance(group.key.temperatureC, lightTemperatureC);
                if (std::isfinite(tdiff) && std::isfinite(best_temp_diff) &&
                    std::abs(tdiff - best_temp_diff) <= kTempAmbiguityEpsilonC)
                {
                    ++near_best;
                }
            }
            match.needsUserAttention = (near_best > 1);
        }
        else
        {
            match.needsUserAttention = (matches > 1);
        }

        if (lightTemperatureC.has_value() && std::isfinite(best_temp_diff))
        {
            // Temperature match contributes modestly to confidence.
            match.confidence = clamp01(1.0 - (best_temp_diff / 10.0));
        }
        else
        {
            match.confidence = 1.0;
        }

        if (match.needsUserAttention)
        {
            match.confidence = (std::min)(match.confidence, 0.75);
            if (m_policy.warnOnFallback)
            {
                match.warningMessage = QStringLiteral("Multiple DARK matches found; review temperature and metadata.");
            }
        }

        return match;
    }

    if (m_policy.allowDarkExposureFallback && m_darkOptions.byExposure)
    {
        return findDarkClosestExposure(lightKey, lightTemperatureC);
    }

    return {};
}

CalibrationMatch CalibrationMatcher::findFlat(const LightGroupKey& lightKey) const
{
    if (auto override_match = findOverride(lightKey, CalibrationOverrideType::Flat))
    {
        // Check for unassigned marker
        if (override_match->masterPath == QString::fromLatin1(kUnassignedCalibrationMarker))
        {
            CalibrationMatch match;
            match.type = CalibrationMatch::MatchType::ManuallyUnassigned;
            match.confidence = 1.0;
            return match;
        }

        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::ManualOverride;
        match.masterPath = override_match->masterPath;
        match.matchedGroupKey = assignedGroupKey(m_flatGroups, *override_match);
        match.confidence = 1.0;
        return match;
    }

    CalibrationMatch exact = findFlatExact(lightKey);
    if (exact.type == CalibrationMatch::MatchType::Exact)
    {
        return exact;
    }

    if (!m_policy.allowFlatSessionFallback || !m_flatOptions.bySession)
    {
        return {};
    }

    return findFlatClosestSession(lightKey);
}

std::optional<CalibrationOverride> CalibrationMatcher::findOverride(const LightGroupKey& lightKey,
                                                                    CalibrationOverrideType type) const
{
    for (const auto& override_entry : m_overrides)
    {
        if (override_entry.type != type)
        {
            continue;
        }

        // Skip overrides with empty paths - treat as no override
        // Note: The unassigned marker (kUnassignedCalibrationMarker) is NOT empty,
        // so it will be returned and handled by the caller.
        if (override_entry.masterPath.isEmpty())
        {
            continue;
        }

        if (lightKeysEqual(override_entry.lightKey, lightKey))
        {
            return override_entry;
        }
    }

    return std::nullopt;
}

CalibrationMatch CalibrationMatcher::findDarkClosestExposure(const LightGroupKey& lightKey,
                                                             std::optional<double> lightTemperatureC) const
{
    if (lightKey.exposureSeconds <= 0.0)
    {
        return {};
    }

    const double tolerance =
        lightKey.exposureSeconds * (static_cast<double>(m_policy.darkExposureTolerancePercent) / 100.0);

    CalibrationMatch best;
    double best_diff = std::numeric_limits<double>::max();
    double best_temp_diff = std::numeric_limits<double>::infinity();
    bool has_best = false;

    CalibrationGroupKey calibration = CalibrationGroupKey::fromLightKey(lightKey);
    for (const auto& group : m_darkGroups)
    {
        DarkGroupKey dark_key = group.key;
        GroupingOptions options = m_darkOptions;
        options.byExposure = false;
        if (!calibration.matchesDark(dark_key, options))
        {
            continue;
        }

        const double diff = std::abs(dark_key.exposureSeconds - lightKey.exposureSeconds);
        const double tdiff = temperatureDistance(dark_key.temperatureC, lightTemperatureC);
        if (diff <= tolerance)
        {
            const bool take_by_exposure = (!has_best || diff < best_diff - kExposureAmbiguityEpsilonSeconds);
            const bool tie_exposure = (has_best && std::abs(diff - best_diff) <= kExposureAmbiguityEpsilonSeconds);
            const bool take_by_temp =
                tie_exposure && std::isfinite(tdiff) &&
                (!std::isfinite(best_temp_diff) || tdiff < best_temp_diff - kTempAmbiguityEpsilonC);

            const bool take_by_key = tie_exposure &&
                                     ((!std::isfinite(tdiff) && !std::isfinite(best_temp_diff)) ||
                                      (std::isfinite(tdiff) && std::isfinite(best_temp_diff) &&
                                       std::abs(tdiff - best_temp_diff) <= kTempAmbiguityEpsilonC)) &&
                                     (!has_best || group.groupKey < GroupKey{best.matchedGroupKey});

            if (take_by_exposure || take_by_temp || take_by_key)
            {
                best_diff = diff;
                best_temp_diff = tdiff;
                best.type = CalibrationMatch::MatchType::ClosestExposure;
                best.masterPath = group.masterPath;
                best.matchedGroupKey = group.groupKey.toString();
                best.exposureDifference = diff;
                has_best = true;
            }
        }
    }

    if (best.type == CalibrationMatch::MatchType::ClosestExposure)
    {
        // Ambiguity: if there are multiple candidates within tolerance and the best isn't clearly better.
        int near_best = 0;
        CalibrationGroupKey calibration2 = CalibrationGroupKey::fromLightKey(lightKey);
        for (const auto& group : m_darkGroups)
        {
            DarkGroupKey dark_key = group.key;
            GroupingOptions options = m_darkOptions;
            options.byExposure = false;
            if (!calibration2.matchesDark(dark_key, options))
            {
                continue;
            }

            const double diff = std::abs(dark_key.exposureSeconds - lightKey.exposureSeconds);
            if (diff > tolerance)
            {
                continue;
            }

            if (std::abs(diff - best_diff) <= kExposureAmbiguityEpsilonSeconds)
            {
                const double tdiff = temperatureDistance(dark_key.temperatureC, lightTemperatureC);
                if (!lightTemperatureC.has_value() || !std::isfinite(best_temp_diff) ||
                    (std::isfinite(tdiff) && std::abs(tdiff - best_temp_diff) <= kTempAmbiguityEpsilonC))
                {
                    ++near_best;
                }
            }
        }
        best.needsUserAttention = (near_best > 1);

        const double exposure_score = clamp01(1.0 - (best.exposureDifference / tolerance));
        const double temp_score = (!lightTemperatureC.has_value() || !std::isfinite(best_temp_diff))
                                      ? 0.5
                                      : clamp01(1.0 - (best_temp_diff / 10.0));
        best.confidence = clamp01(0.5 * exposure_score + 0.5 * temp_score) * 0.9;
        if (best.needsUserAttention)
        {
            best.confidence = (std::min)(best.confidence, 0.6);
        }

        best = makeFallbackWarning(
            best, m_policy, QStringLiteral("Using DARK with exposure difference %1s").arg(best.exposureDifference));
        return best;
    }

    return {};
}

CalibrationMatch CalibrationMatcher::findFlatExact(const LightGroupKey& lightKey) const
{
    CalibrationGroupKey calibration = CalibrationGroupKey::fromLightKey(lightKey);
    int matches = 0;
    FlatCalibrationGroup best_group;
    for (const auto& group : m_flatGroups)
    {
        if (calibration.matchesFlat(group.key, m_flatOptions))
        {
            if (matches == 0 || group.groupKey < best_group.groupKey)
            {
                best_group = group;
            }
            ++matches;
        }
    }

    if (matches > 0)
    {
        CalibrationMatch match;
        match.type = CalibrationMatch::MatchType::Exact;
        match.masterPath = best_group.masterPath;
        match.matchedGroupKey = best_group.groupKey.toString();
        match.needsUserAttention = (matches > 1);
        match.confidence = match.needsUserAttention ? 0.8 : 1.0;
        if (match.needsUserAttention && m_policy.warnOnFallback)
        {
            match.warningMessage = QStringLiteral("Multiple FLAT matches found; using deterministic selection.");
        }
        return match;
    }

    return {};
}

CalibrationMatch CalibrationMatcher::findFlatClosestSession(const LightGroupKey& lightKey) const
{
    const QDate target_date = QDate::fromString(lightKey.session, QStringLiteral("yyyy-MM-dd"));
    if (!target_date.isValid())
    {
        return {};
    }

    CalibrationMatch best;
    int best_diff = std::numeric_limits<int>::max();
    QDate best_date;

    for (const auto& group : m_flatGroups)
    {
        if (!matchesExceptSession(group.key, lightKey, m_flatOptions))
        {
            continue;
        }

        const QDate group_date = QDate::fromString(group.key.session, QStringLiteral("yyyy-MM-dd"));
        if (!group_date.isValid())
        {
            continue;
        }

        const int diff = std::abs(target_date.daysTo(group_date));
        if (diff > m_policy.maxFlatSessionDaysApart)
        {
            continue;
        }

        bool take = false;
        if (diff < best_diff)
        {
            take = true;
        }
        else if (diff == best_diff)
        {
            if (m_policy.preferNewerFlat && group_date > best_date)
            {
                take = true;
            }
            else if (!m_policy.preferNewerFlat && group_date < best_date)
            {
                take = true;
            }
            else if (group.groupKey < GroupKey{best.matchedGroupKey})
            {
                take = true;
            }
        }

        if (take)
        {
            best_diff = diff;
            best_date = group_date;
            best = {CalibrationMatch::MatchType::ClosestDate,
                    group.masterPath,
                    group.groupKey.toString(),
                    diff,
                    0.0,
                    QString()};
        }
    }

    if (best.type == CalibrationMatch::MatchType::ClosestDate)
    {
        // Confidence decreases as the date delta approaches the allowed limit.
        best.confidence = clamp01(1.0 - (static_cast<double>(best.dateDifferencesDays) /
                                         (std::max)(1, m_policy.maxFlatSessionDaysApart))) *
                          0.9;

        best = makeFallbackWarning(best, m_policy,
                                   QStringLiteral("Using FLAT with session %1 (%2 days)")
                                       .arg(best.matchedGroupKey)
                                       .arg(best.dateDifferencesDays));
        return best;
    }

    return {};
}
