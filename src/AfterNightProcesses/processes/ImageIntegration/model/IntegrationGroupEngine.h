// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "GroupingKeys.h"
#include "GroupingOptions.h"

#include <map>
#include <vector>

class AFTERNIGHTPROCESSES_API IntegrationGroupEngine
{
public:
    using GroupMap = std::map<IntegrationGroupKey, std::vector<FrameId>>;

    [[nodiscard]] GroupMap computeGroups(const std::vector<FrameInfo>& lightFrames,
                                         const GroupingOptions& options) const;

    /// The integration group a light joins: its manual group when it has one, else the group of its resolution,
    /// filter and object and of whatever else the options group by. The key of a light `computeGroups` leaves out
    /// because it is excluded is the group it would have joined.
    [[nodiscard]] static IntegrationGroupKey groupKeyFor(const FrameInfo& frame, const GroupingOptions& options);
};
