// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QStringList>

#include "controls/CalibrationPreviewConfig.h"

namespace an::ui::detail
{

[[nodiscard]] inline bool calibrationPreviewHasInput(const CalibrationPreviewConfig& config)
{
    return !config.biasPath.isEmpty() || !config.darkPath.isEmpty() || !config.flatPath.isEmpty();
}

[[nodiscard]] inline bool calibrationPreviewIsEnabled(const CalibrationPreviewConfig& config, const bool checked)
{
    return checked && config.available && calibrationPreviewHasInput(config);
}

[[nodiscard]] inline QString buildCalibrationPreviewKey(const CalibrationPreviewConfig& config, const bool checked)
{
    if (!calibrationPreviewIsEnabled(config, checked))
    {
        return {};
    }

    const auto& settings = config.settings;
    QStringList parts;
    parts << config.biasPath << config.darkPath << config.flatPath
          << (config.subtractBiasFromDark ? QStringLiteral("1") : QStringLiteral("0"))
          << (settings.cosmeticCorrection ? QStringLiteral("1") : QStringLiteral("0"))
          << QString::number(settings.sigmaCold, 'f', 6) << QString::number(settings.sigmaHot, 'f', 6)
          << (settings.darkOptimization ? QStringLiteral("1") : QStringLiteral("0"))
          << QString::number(settings.darkMultiplicationFactor, 'f', 6)
          << (settings.flatAutoLevel ? QStringLiteral("1") : QStringLiteral("0"))
          << QString::number(settings.pedestal, 'f', 6);
    return parts.join(QStringLiteral("|"));
}

} // namespace an::ui::detail
