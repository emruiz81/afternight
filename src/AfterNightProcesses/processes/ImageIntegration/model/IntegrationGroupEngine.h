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
};
