// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file GroupingOptions.h
 * @brief Configuration flags for frame grouping criteria.
 *
 * GroupingOptions controls which metadata fields are considered when computing
 * frame groups. By enabling/disabling specific criteria, users can customize
 * how frames are organized for calibration matching and stacking.
 *
 * @par Default Behavior
 * By default, frames are grouped by resolution, filter, object, and session.
 * ISO/gain, exposure, and custom tags require explicit opt-in.
 *
 * @see GroupingEngine, GroupingKeys, CalibrationMatcher
 */

/**
 * @brief Controls which metadata fields participate in grouping.
 *
 * Each boolean flag determines whether a specific metadata field should be
 * considered when computing frame group membership. Frames with matching
 * values for all enabled criteria will be placed in the same group.
 */
struct GroupingOptions
{
    bool byResolution = true; ///< Group by image dimensions
    bool byFilter = true; ///< Group by filter name
    bool byObject = true; ///< Group by target object name
    bool bySession = true; ///< Group by imaging session
    bool byIsoGain = false; ///< Group by ISO/gain value (disabled by default)
    bool byOffset = true; ///< Group by sensor offset (if available)
    bool byExposure = false; ///< Group by exposure time (disabled by default)
    double exposureToleranceSeconds = 3.0; ///< Exposure grouping tolerance in seconds (0 = exact)
    bool byTemperature = false; ///< Group by sensor temperature (if available, disabled by default)
    double temperatureToleranceC = 2.0; ///< Temperature grouping tolerance in Celsius (0 = exact)
    bool byCustomTag = false; ///< Group by user-defined custom tag
};

[[nodiscard]] inline bool operator==(const GroupingOptions& lhs, const GroupingOptions& rhs) noexcept
{
    return lhs.byResolution == rhs.byResolution && lhs.byFilter == rhs.byFilter && lhs.byObject == rhs.byObject &&
           lhs.bySession == rhs.bySession && lhs.byIsoGain == rhs.byIsoGain && lhs.byOffset == rhs.byOffset &&
           lhs.byExposure == rhs.byExposure && lhs.exposureToleranceSeconds == rhs.exposureToleranceSeconds &&
           lhs.byTemperature == rhs.byTemperature && lhs.temperatureToleranceC == rhs.temperatureToleranceC &&
           lhs.byCustomTag == rhs.byCustomTag;
}

[[nodiscard]] inline bool operator!=(const GroupingOptions& lhs, const GroupingOptions& rhs) noexcept
{
    return !(lhs == rhs);
}
