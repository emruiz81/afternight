// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file GroupingEngine.h
 * @brief Deterministic frame grouping engine for calibration and stacking.
 *
 * GroupingEngine computes frame groups based on configurable matching criteria.
 * Groups are deterministic given the same inputs, ensuring reproducible results
 * across sessions and platforms.
 *
 * @par Grouping Algorithm
 * The engine computes a GroupKey for each frame based on enabled matching
 * criteria (dimensions, ISO/gain, offset, filter, exposure, temperature).
 * Frames with identical GroupKeys are placed in the same group.
 *
 * @par Performance
 * Grouping is O(n) where n is the number of frames. The engine is stateless
 * and can be reused across multiple grouping operations.
 *
 * @see GroupingOptions, GroupKey, CalibrationMatcher
 */

#include "GroupingKeys.h"

#include <vector>

/**
 * @brief Result set for grouped frames.
 *
 * Contains a group key and all frames that share that key based on the
 * active grouping criteria.
 */
struct GroupedFrameSet
{
    GroupKey key; ///< The common key shared by all frames
    std::vector<FrameInfo> frames; ///< Frames belonging to this group
};

/**
 * @brief Stateless engine for computing deterministic frame groups.
 *
 * The GroupingEngine is the core component responsible for organizing frames
 * into logical groups for calibration matching and stacking. It uses the
 * GroupingOptions to determine which metadata fields should be considered
 * when computing group membership.
 *
 * @par Example
 * @code
 * GroupingEngine engine;
 * GroupingOptions options;
 * options.matchExposure = true;
 * options.exposureToleranceSec = 0.1;
 *
 * auto groups = engine.groupFrames(darkFrames, FrameType::Dark, options);
 * // groups now contains dark frames grouped by exposure time
 * @endcode
 */
class AFTERNIGHTPROCESSES_API GroupingEngine
{
public:
    /**
     * @brief Group frames into sets based on matching criteria.
     * @param frames The frames to group (must all be of the same type).
     * @param type The frame type for type-specific grouping rules.
     * @param options The grouping criteria configuration.
     * @return Vector of grouped frame sets, each with a unique GroupKey.
     * @note Groups are sorted by their string representation for determinism.
     */
    [[nodiscard]] std::vector<GroupedFrameSet> groupFrames(const std::vector<FrameInfo>& frames, FrameType type,
                                                           const GroupingOptions& options) const;

    /**
     * @brief Compute the group key for a single frame.
     * @param frame The frame to compute a key for.
     * @param type The frame type for type-specific rules.
     * @param options The grouping criteria configuration.
     * @return The computed GroupKey based on active matching criteria.
     */
    [[nodiscard]] GroupKey computeGroupKey(const FrameInfo& frame, FrameType type,
                                           const GroupingOptions& options) const;

    /**
     * @brief Checks if a frame can be moved to a destination group.
     *
     * A frame is compatible if its hardware metadata (resolution, filter, etc.)
     * matches the destination group's criteria. Only Object and Session
     * are allowed to differ.
     */
    [[nodiscard]] bool checkCompatibilityForMove(const FrameInfo& frame, const GroupKey& targetKey, FrameType type,
                                                 const GroupingOptions& options) const;
};
