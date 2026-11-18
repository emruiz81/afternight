// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/RailItem.h"

namespace an::ui
{

bool sameRailItem(const RailItem& a, const RailItem& b)
{
    return a.id == b.id && a.section == b.section && a.iconKey == b.iconKey && a.text == b.text &&
           a.badgeText == b.badgeText && a.selected == b.selected && a.enabled == b.enabled &&
           a.thumbnail.cacheKey() == b.thumbnail.cacheKey() && a.glyphKey == b.glyphKey && a.runPhase == b.runPhase &&
           a.progress == b.progress && a.runText == b.runText && a.unseenResult == b.unseenResult &&
           a.floating == b.floating;
}

QString railItemDescription(const RailItem& item)
{
    QString description = item.text;
    if (item.section != RailSection::Task)
    {
        return description;
    }
    switch (item.runPhase)
    {
    case RailRunPhase::Running: {
        const QString does = item.runText.isEmpty() ? QStringLiteral("applying") : item.runText;
        description += item.progress ? QStringLiteral(" - %1 %2 %").arg(does).arg(*item.progress)
                                     : QStringLiteral(" - %1").arg(does);
        break;
    }
    case RailRunPhase::Succeeded:
        if (item.unseenResult)
        {
            description += QStringLiteral(" - applied");
        }
        break;
    case RailRunPhase::Failed:
        if (item.unseenResult)
        {
            description += QStringLiteral(" - failed");
        }
        break;
    case RailRunPhase::None:
        break;
    }
    if (item.floating)
    {
        description += QStringLiteral(" - in a window");
    }
    return description;
}

} // namespace an::ui
