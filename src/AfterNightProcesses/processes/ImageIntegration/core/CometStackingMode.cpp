// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comet stacking modes' spellings.

#include "stdafx.hpp"
#include "processes/ImageIntegration/core/CometStackingMode.h"

#include "processes/core/WorkflowSettingsRecords.h"

namespace ImageIntegration
{

QString normalizeCometStackingMode(const QString& mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("star") || normalized == QStringLiteral("stars") ||
        normalized == QStringLiteral("star_aligned"))
    {
        return QStringLiteral("Stars");
    }
    if (normalized == QStringLiteral("comet") || normalized == QStringLiteral("comet only") ||
        normalized == QStringLiteral("comet_only") || normalized == QStringLiteral("comet-aligned") ||
        normalized == QStringLiteral("comet_aligned") || normalized == QStringLiteral("purecomet") ||
        normalized == QStringLiteral("pure_comet") || normalized == QStringLiteral("pure-comet"))
    {
        return QStringLiteral("Comet");
    }
    if (isStarsPlusCometExperimentalStackingMode(mode))
    {
        return starsPlusCometExperimentalStackingModeToken();
    }
    return mode.trimmed();
}

QString starsPlusCometExperimentalStackingModeToken()
{
    // Hidden internal token only. This implementation is not reliable enough for the user-facing UI yet; keep it
    // behind the test-only runtime gate until Stars + Comet is rebuilt as a first-class compositor workflow.
    return QStringLiteral("StarsPlusCometExperimental");
}

bool isStarsPlusCometExperimentalStackingMode(const QString& mode)
{
    return mode.trimmed() == starsPlusCometExperimentalStackingModeToken();
}

bool usesStarsPlusCometExperimentalStackingMode(const QString& mode)
{
    return isStarsPlusCometExperimentalStackingMode(mode);
}

bool usesCometOnlyStackingMode(const QString& mode)
{
    return normalizeCometStackingMode(mode) == QStringLiteral("Comet");
}

bool usesCometStackingMode(const QString& mode)
{
    return usesCometOnlyStackingMode(mode) || usesStarsPlusCometExperimentalStackingMode(mode);
}

bool registrationRequestsStarsPlusCometExperimentalOutput(const RegistrationSettings& settings)
{
    return settings.cometTracking.has_value() &&
           isStarsPlusCometExperimentalStackingMode(settings.cometTracking->outputMode);
}

} // namespace ImageIntegration
