// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file CalibrationMatcher.h
 * @brief Intelligent calibration frame matching with fallback support.
 *
 * CalibrationMatcher finds the best matching calibration master (bias, dark,
 * flat) for each light frame group. It supports exact matching based on
 * grouping criteria, as well as intelligent fallback matching when exact
 * matches are not available.
 *
 * @par Matching Strategy
 * 1. Check for manual overrides first
 * 2. Attempt exact match based on grouping options
 * 3. Apply fallback rules (closest date, exposure tolerance)
 * 4. Return no match if all strategies fail
 *
 * @par Fallback Behavior
 * - Dark frames: Allow exposure tolerance (e.g., ±20%)
 * - Flat frames: Allow session date proximity (e.g., within 30 days)
 * - Bias frames: Strict matching only (no fallback)
 *
 * @see GroupingKeys, CalibrationMatchingPolicy, IntegrationDataModel
 */

#include "GroupingKeys.h"
#include "GroupingOptions.h"

#include <QString>

#include <optional>
#include <vector>

/**
 * @brief Policy for calibration fallback matching behavior.
 *
 * Defines tolerance levels and preferences for fallback matching when
 * exact calibration matches are not available.
 */
struct CalibrationMatchingPolicy
{
    bool allowFlatSessionFallback = true;
    int maxFlatSessionDaysApart = 30;
    bool preferNewerFlat = false;

    bool allowDarkExposureFallback = true;
    int darkExposureTolerancePercent = 20;

    bool warnOnFallback = true;

    // CCD mode.
    bool subtractBiasFromDark = false;
};

enum class CalibrationOverrideType
{
    Bias,
    Dark,
    Flat
};

struct CalibrationOverride
{
    LightGroupKey lightKey;
    CalibrationOverrideType type = CalibrationOverrideType::Bias;
    QString masterPath;
    GroupKey groupKey;
};

/// Sentinel path value used to indicate a manually unassigned calibration override.
/// When this value is set as the masterPath of an override, it means the user explicitly
/// chose not to use any calibration master for that type, rather than auto-detection.
inline constexpr char kUnassignedCalibrationMarker[] = "::unassigned::";

struct CalibrationMatch
{
    enum class MatchType
    {
        Exact,
        ClosestDate,
        ClosestExposure,
        ManualOverride,
        ManuallyUnassigned, ///< User explicitly disabled this calibration type
        None
    };

    MatchType type = MatchType::None;
    QString masterPath;
    QString matchedGroupKey;
    int dateDifferencesDays = 0;
    double exposureDifference = 0.0;
    QString warningMessage;

    // 0..1 estimate of match quality (1.0 = best / exact).
    double confidence = 0.0;

    // True when the match is ambiguous and should be reviewed by the user.
    bool needsUserAttention = false;
};

/// Whether a match applies a master: one did match, and the person did not unassign the kind.
[[nodiscard]] inline bool appliesMaster(const CalibrationMatch& match) noexcept
{
    return match.type != CalibrationMatch::MatchType::None &&
           match.type != CalibrationMatch::MatchType::ManuallyUnassigned;
}

/// Whether a light takes the bias master beside what `darkMatch` gives it. A master dark holds the bias, so a light
/// takes the bias where no dark is applied to it - none matched, or the person unassigned it - and in CCD mode, which
/// subtracts the bias from the dark where the light is calibrated.
[[nodiscard]] inline bool lightTakesBias(const CalibrationMatch& darkMatch, bool subtractBiasFromDark) noexcept
{
    return subtractBiasFromDark || !appliesMaster(darkMatch);
}

/// Whether a flat takes the bias master beside what `darkMatch` gives it: only where no dark is applied to it - none
/// matched, or the person unassigned it - since a flat takes its dark, which holds the bias, alone, in CCD mode too.
[[nodiscard]] inline bool flatTakesBias(const CalibrationMatch& darkMatch) noexcept
{
    return !appliesMaster(darkMatch);
}

struct BiasCalibrationGroup
{
    BiasGroupKey key;
    GroupKey groupKey;
    QString masterPath;
};

struct DarkCalibrationGroup
{
    DarkGroupKey key;
    GroupKey groupKey;
    QString masterPath;
};

struct FlatCalibrationGroup
{
    FlatGroupKey key;
    GroupKey groupKey;
    QString masterPath;
};

/// Matches light groups to calibration masters using options and fallback policy.
class AFTERNIGHTPROCESSES_API CalibrationMatcher
{
public:
    CalibrationMatcher(GroupingOptions biasOptions, GroupingOptions darkOptions, GroupingOptions flatOptions,
                       CalibrationMatchingPolicy policy = {});

    void setBiasGroups(std::vector<BiasCalibrationGroup> groups);
    void setDarkGroups(std::vector<DarkCalibrationGroup> groups);
    void setFlatGroups(std::vector<FlatCalibrationGroup> groups);
    void setOverrides(std::vector<CalibrationOverride> overrides);

    [[nodiscard]] CalibrationMatch findBias(const LightGroupKey& lightKey) const;
    [[nodiscard]] CalibrationMatch findDark(const LightGroupKey& lightKey) const;
    [[nodiscard]] CalibrationMatch findDark(const LightGroupKey& lightKey,
                                            std::optional<double> lightTemperatureC) const;
    [[nodiscard]] CalibrationMatch findFlat(const LightGroupKey& lightKey) const;

private:
    [[nodiscard]] std::optional<CalibrationOverride> findOverride(const LightGroupKey& lightKey,
                                                                  CalibrationOverrideType type) const;
    [[nodiscard]] CalibrationMatch findDarkClosestExposure(const LightGroupKey& lightKey,
                                                           std::optional<double> lightTemperatureC) const;
    [[nodiscard]] CalibrationMatch findFlatExact(const LightGroupKey& lightKey) const;
    [[nodiscard]] CalibrationMatch findFlatClosestSession(const LightGroupKey& lightKey) const;

    GroupingOptions m_biasOptions;
    GroupingOptions m_darkOptions;
    GroupingOptions m_flatOptions;
    CalibrationMatchingPolicy m_policy;

    std::vector<BiasCalibrationGroup> m_biasGroups;
    std::vector<DarkCalibrationGroup> m_darkGroups;
    std::vector<FlatCalibrationGroup> m_flatGroups;
    std::vector<CalibrationOverride> m_overrides;
};
