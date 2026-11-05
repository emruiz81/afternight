// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QString>
#include <QStringList>

namespace ImageIntegration
{
[[nodiscard]] AFTERNIGHTPROCESSES_API bool shouldShowRemoveFramesConfirmation(int frameCount,
                                                                              bool dontAskAgainForSession);
[[nodiscard]] AFTERNIGHTPROCESSES_API QString buildRemoveFramesConfirmationMessage(const QStringList& selectedPaths,
                                                                                   int maxNamesToShow = 10);

/// Details about calibration relationships that would be affected by a type change.
struct TypeChangeConflictInfo
{
    QStringList affectedLightGroups; ///< Light groups that reference the frames being moved
    QStringList affectedFlatGroups; ///< Flat groups that reference the frames being moved
    QStringList orphanedOverrides; ///< Manual overrides that would become invalid
    bool hasConflicts = false; ///< True if any conflicts were detected
};

/// Builds a user-friendly message explaining the type change conflicts.
[[nodiscard]] QString buildTypeChangeConflictMessage(const QStringList& paths, const QString& fromType,
                                                     const QString& toType, const TypeChangeConflictInfo& conflicts);
} // namespace ImageIntegration
