// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "GroupingKeys.h"

#include <map>
#include <vector>

class AFTERNIGHTPROCESSES_API RegistrationGroupEngine
{
public:
    using GroupMap = std::map<RegistrationGroupKey, std::vector<FrameId>>;

    [[nodiscard]] GroupMap computeGroups(const std::vector<FrameInfo>& lightFrames) const;

    // Stable machine key used by workflow/metrics DB.
    [[nodiscard]] static GroupKey toMachineGroupKey(const RegistrationGroupKey& key);
};
