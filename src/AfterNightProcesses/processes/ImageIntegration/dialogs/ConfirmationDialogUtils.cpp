// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ConfirmationDialogUtils.h"

#include <QFileInfo>

#include <algorithm>

namespace ImageIntegration
{
bool shouldShowRemoveFramesConfirmation(int frameCount, bool dontAskAgainForSession)
{
    if (dontAskAgainForSession)
    {
        return false;
    }

    return frameCount > 10;
}

QString buildRemoveFramesConfirmationMessage(const QStringList& selectedPaths, int maxNamesToShow)
{
    QString msg = QObject::tr("Are you sure you want to remove %1 selected frame(s)?").arg(selectedPaths.size());

    if (!selectedPaths.isEmpty())
    {
        QStringList lines;
        const int selectedCount = static_cast<int>(selectedPaths.size());
        const int showCount = (std::min)(maxNamesToShow, selectedCount);
        for (int i = 0; i < showCount; ++i)
        {
            const QString name = QFileInfo(selectedPaths[i]).fileName();
            lines.append(QObject::tr("• %1").arg(name.isEmpty() ? selectedPaths[i] : name));
        }

        const int remaining = static_cast<int>(selectedPaths.size()) - showCount;
        if (remaining > 0)
        {
            lines.append(QObject::tr("• ...and %1 more").arg(remaining));
        }

        msg += QObject::tr("\n\n%1").arg(lines.join(QStringLiteral("\n")));
    }

    msg += QObject::tr("\n\nYou can undo this action with Ctrl+Z.");
    return msg;
}

QString buildTypeChangeConflictMessage(const QStringList& paths, const QString& fromType, const QString& toType,
                                       const TypeChangeConflictInfo& conflicts)
{
    QString msg = QObject::tr("Moving %1 frame(s) from %2 to %3.").arg(paths.size()).arg(fromType).arg(toType);

    if (!conflicts.hasConflicts)
    {
        msg += QObject::tr("\n\nNo calibration relationships will be affected.");
        return msg;
    }

    msg += QObject::tr("\n\n⚠ This will affect existing calibration assignments:");

    if (!conflicts.affectedLightGroups.isEmpty())
    {
        const int count = conflicts.affectedLightGroups.size();
        msg += QObject::tr("\n\n• %1 light group(s) use this calibration:").arg(count);
        const int maxShow = (std::min)(5, count);
        for (int i = 0; i < maxShow; ++i)
        {
            msg += QObject::tr("\n   - %1").arg(conflicts.affectedLightGroups[i]);
        }
        if (count > maxShow)
        {
            msg += QObject::tr("\n   - ...and %1 more").arg(count - maxShow);
        }
    }

    if (!conflicts.affectedFlatGroups.isEmpty())
    {
        const int count = conflicts.affectedFlatGroups.size();
        msg += QObject::tr("\n\n• %1 flat group(s) use this calibration:").arg(count);
        const int maxShow = (std::min)(5, count);
        for (int i = 0; i < maxShow; ++i)
        {
            msg += QObject::tr("\n   - %1").arg(conflicts.affectedFlatGroups[i]);
        }
        if (count > maxShow)
        {
            msg += QObject::tr("\n   - ...and %1 more").arg(count - maxShow);
        }
    }

    if (!conflicts.orphanedOverrides.isEmpty())
    {
        const int count = conflicts.orphanedOverrides.size();
        msg += QObject::tr("\n\n• %1 manual override(s) will become invalid").arg(count);
    }

    msg += QObject::tr("\n\nThese assignments will need to be reconfigured. Continue?");

    return msg;
}
} // namespace ImageIntegration
