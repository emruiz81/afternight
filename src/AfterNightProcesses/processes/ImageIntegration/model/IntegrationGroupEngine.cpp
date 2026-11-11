// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegrationGroupEngine.h"

IntegrationGroupKey IntegrationGroupEngine::groupKeyFor(const FrameInfo& frame, const GroupingOptions& options)
{
    // Use manual group key if present (overrides auto-grouping)
    if (frame.manualGroupKey.has_value())
    {
        return IntegrationGroupKey{frame.manualGroupKey.value()};
    }

    // Enforce requirements: Resolution, Filter, and Object are always used for integration groups
    GroupingOptions effectiveOptions = options;
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;

    // Compute key using LightGroupKey logic but with forced options
    const LightGroupKey lightKey = LightGroupKey::fromFrame(frame);
    return IntegrationGroupKey{lightKey.toGroupKey(effectiveOptions)};
}

IntegrationGroupEngine::GroupMap IntegrationGroupEngine::computeGroups(const std::vector<FrameInfo>& lightFrames,
                                                                       const GroupingOptions& options) const
{
    GroupMap groups;

    for (const auto& frame : lightFrames)
    {
        if (frame.excluded)
            continue;

        groups[groupKeyFor(frame, options)].push_back(frame.id);
    }

    // Sort frames within each group for determinism
    for (auto& [key, frames] : groups)
    {
        std::sort(frames.begin(), frames.end());
    }

    return groups;
}
