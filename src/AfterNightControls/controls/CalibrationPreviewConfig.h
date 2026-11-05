// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>

namespace an::ui
{

struct CalibrationPreviewSettings
{
    bool cosmeticCorrection = true;
    double sigmaCold = 3.0;
    double sigmaHot = 3.0;
    bool darkOptimization = false;
    double darkMultiplicationFactor = 1.0;
    bool flatAutoLevel = true;
    double pedestal = 0.0;
};

struct CalibrationPreviewConfig
{
    bool available = false;
    QString tooltip;
    QString biasPath;
    QString darkPath;
    QString flatPath;
    bool subtractBiasFromDark = false;
    CalibrationPreviewSettings settings;
};

} // namespace an::ui
