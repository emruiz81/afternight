// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"

StackingSettings defaultCalibrationStackingSettings(FrameType frameType)
{
    StackingSettings settings;

    switch (frameType)
    {
    case FrameType::Flat:
        settings.rejectionMethod = QStringLiteral("Sigma Clipping");
        settings.normalizationMode = NormalizationMode::Global;
        settings.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
        settings.localNormalization.enabled = false;
        break;
    case FrameType::Bias:
    case FrameType::Dark:
        settings.rejectionMethod = QStringLiteral("Sigma Clipping");
        settings.normalizationMode = NormalizationMode::None;
        settings.localNormalization.enabled = false;
        break;
    case FrameType::Light:
        // Keep existing global defaults for Light integration groups.
        break;
    }

    return settings;
}

StackingSettings normalizeCalibrationStackingSettings(FrameType frameType, const StackingSettings& settings)
{
    StackingSettings normalized = settings;

    switch (frameType)
    {
    case FrameType::Bias:
    case FrameType::Dark:
        normalized.normalizationMode = NormalizationMode::None;
        normalized.localNormalization.enabled = false;
        normalized.createQualityMaps = false;
        break;
    case FrameType::Flat:
        normalized.createQualityMaps = false;
        break;
    case FrameType::Light:
        break;
    }

    return normalized;
}

QString calibrationMasterOutputFilename(CalibrationOverrideType type, const GroupKey& groupKey,
                                        const GroupingOptions& options, FrameType frameType)
{
    QString prefix;
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        prefix = QStringLiteral("master_bias");
        break;
    case CalibrationOverrideType::Dark:
        prefix = QStringLiteral("master_dark");
        break;
    case CalibrationOverrideType::Flat:
        prefix = QStringLiteral("master_flat");
        break;
    }

    const QString display = groupKey.toDisplayString(options, frameType);
    return QStringLiteral("%1_%2.fit").arg(prefix, ImageIntegration::sanitizeFilename(display));
}

std::optional<double> averageTemperatureC(const std::vector<FrameInfo>& frames)
{
    double sum = 0.0;
    int count = 0;
    for (const auto& frame : frames)
    {
        if (auto t = frame.metadata.temperatureC())
        {
            sum += *t;
            ++count;
        }
    }
    if (count == 0)
    {
        return std::nullopt;
    }
    return sum / static_cast<double>(count);
}
