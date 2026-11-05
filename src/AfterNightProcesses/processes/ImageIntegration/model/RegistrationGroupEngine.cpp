// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationGroupEngine.h"

#include <algorithm>

namespace
{
QString normalizeTokenForKey(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
    {
        return QStringLiteral("-");
    }

    // Avoid delimiter collisions with GroupKey's '|' separator.
    value.replace(QLatin1Char('|'), QLatin1Char('_'));
    return value;
}
} // namespace

RegistrationGroupEngine::GroupMap
RegistrationGroupEngine::computeGroups(const std::vector<FrameInfo>& lightFrames) const
{
    GroupMap groups;

    for (const auto& frame : lightFrames)
    {
        if (frame.excluded)
        {
            continue;
        }

        const RegistrationGroupKey key = RegistrationGroupKey::fromFrame(frame);
        groups[key].push_back(frame.id);
    }

    for (auto& entry : groups)
    {
        auto& ids = entry.second;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }

    return groups;
}

GroupKey RegistrationGroupEngine::toMachineGroupKey(const RegistrationGroupKey& key)
{
    return GroupKey{
        QStringLiteral("reg|%1|%2").arg(normalizeTokenForKey(key.resolution), normalizeTokenForKey(key.objectName))};
}
