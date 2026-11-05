// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegrationGroupEngine.h"

IntegrationGroupEngine::GroupMap IntegrationGroupEngine::computeGroups(const std::vector<FrameInfo>& lightFrames,
                                                                       const GroupingOptions& options) const
{
    GroupMap groups;

    // Enforce requirements: Resolution, Filter, and Object are always used for integration groups
    GroupingOptions effectiveOptions = options;
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;

    for (const auto& frame : lightFrames)
    {
        if (frame.excluded)
            continue;

        // Use manual group key if present (overrides auto-grouping)
        if (frame.manualGroupKey.has_value())
        {
            IntegrationGroupKey key{frame.manualGroupKey.value()};
            groups[key].push_back(frame.id);
            continue;
        }

        // Compute key using LightGroupKey logic but with forced options
        LightGroupKey lightKey = LightGroupKey::fromFrame(frame);
        GroupKey groupKey = lightKey.toGroupKey(effectiveOptions);

        IntegrationGroupKey key{groupKey};
        groups[key].push_back(frame.id);
    }

    // Sort frames within each group for determinism
    for (auto& [key, frames] : groups)
    {
        std::sort(frames.begin(), frames.end());
    }

    return groups;
}
