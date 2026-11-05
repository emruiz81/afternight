// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/GaiaCatalogSettings.h"

#include <QFileInfo>
#include <QString>

namespace an::ui
{

[[nodiscard]] inline bool isConfiguredExecutablePath(QString path)
{
    path = path.trimmed();
    if (path.isEmpty())
    {
        return false;
    }

    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

[[nodiscard]] inline bool isInternalGaiaSolverConfigured(const PlateSolveSettings& settings)
{
    return isGaiaCatalogReady(settings);
}

[[nodiscard]] inline bool isAstapSolverConfigured(const PlateSolveSettings& settings)
{
    return isConfiguredExecutablePath(settings.astapPath);
}

[[nodiscard]] inline bool isAstrometryLocalSolverConfigured(const PlateSolveSettings& settings)
{
    return isConfiguredExecutablePath(settings.astrometryLocalPath);
}

[[nodiscard]] inline bool isAstrometryWebSolverConfigured(const PlateSolveSettings& settings)
{
    return !settings.astrometryApiKey.trimmed().isEmpty();
}

[[nodiscard]] inline bool isPlateSolveSolverConfigured(const PlateSolveSettings& settings, QString solver)
{
    solver = normalizePlateSolveDefaultSolver(std::move(solver));
    if (solver == QStringLiteral("internal_gaia"))
    {
        return isInternalGaiaSolverConfigured(settings);
    }
    if (solver == QStringLiteral("astap"))
    {
        return isAstapSolverConfigured(settings);
    }
    if (solver == QStringLiteral("astrometry_local"))
    {
        return isAstrometryLocalSolverConfigured(settings);
    }
    if (solver == QStringLiteral("astrometry_web"))
    {
        return isAstrometryWebSolverConfigured(settings);
    }
    return false;
}

[[nodiscard]] inline QString preferredConfiguredPlateSolveSolver(const PlateSolveSettings& settings,
                                                                 QString preferred = QString())
{
    preferred = normalizePlateSolveDefaultSolver(preferred.isNull() ? settings.defaultSolver : preferred);
    if (!preferred.isEmpty() && isPlateSolveSolverConfigured(settings, preferred))
    {
        return preferred;
    }

    static const QString solver_order[] = {
        QStringLiteral("internal_gaia"),
        QStringLiteral("astap"),
        QStringLiteral("astrometry_local"),
        QStringLiteral("astrometry_web"),
    };

    for (const QString& solver : solver_order)
    {
        if (isPlateSolveSolverConfigured(settings, solver))
        {
            return solver;
        }
    }

    return QString();
}

[[nodiscard]] inline bool hasAnyConfiguredPlateSolveSolver(const PlateSolveSettings& settings)
{
    return !preferredConfiguredPlateSolveSolver(settings).isEmpty();
}

} // namespace an::ui
