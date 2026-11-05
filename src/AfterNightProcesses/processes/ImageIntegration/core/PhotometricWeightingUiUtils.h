// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The photometric weighting's inputs as the integration's windows and its stack read them from a frame's registration
/// metrics: the engine's per-frame measurement, and how much of a set carries each figure.

#include "processes/ImageIntegration/model/FrameQuality.h"

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>

#include <QList>
#include <QString>

#include <cmath>
#include <optional>
#include <vector>

namespace ImageIntegration
{

[[nodiscard]] inline bool isPhotometricWeightingMode(const QString& weightingMode)
{
    return weightingMode.trimmed().compare(QStringLiteral("Photometric"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline bool hasFiniteNonZeroMetric(const double value)
{
    return std::isfinite(value) && std::abs(value) > 1.0e-12;
}

[[nodiscard]] inline bool hasFinitePositiveMetric(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

/// The engine's photometric measurement of a frame from its registration metrics: the zero point, its scatter as the
/// standard uncertainty and the astrometric residual, each only when the metrics carry it.
[[nodiscard]] inline an::PhotometricFrameMeasurement toPhotometricFrameMeasurement(const FrameQualityMetrics& metrics)
{
    an::PhotometricFrameMeasurement measurement;
    if (hasFiniteNonZeroMetric(metrics.photometric_zero_point))
    {
        measurement.zeroPointMagnitudes = metrics.photometric_zero_point;
    }
    if (hasFinitePositiveMetric(metrics.zero_point_scatter))
    {
        measurement.zeroPointStandardUncertaintyMagnitudes = metrics.zero_point_scatter;
    }
    if (hasFinitePositiveMetric(metrics.astrometric_rms_pixels))
    {
        measurement.astrometricRootMeanSquarePixels = metrics.astrometric_rms_pixels;
    }
    return measurement;
}

struct PhotometricCoverageSummary
{
    int includedFrames = 0;
    int framesWithZeroPoint = 0;
    int framesWithScatter = 0;
    int framesWithRms = 0;

    [[nodiscard]] bool photometricWeightingAvailable() const
    {
        return framesWithZeroPoint >= 2;
    }

    [[nodiscard]] bool hasAnyZeroPointData() const
    {
        return framesWithZeroPoint > 0;
    }

    [[nodiscard]] bool hasHardWarning() const
    {
        return includedFrames < 2 || framesWithZeroPoint < 2 ||
               (includedFrames > 0 && (framesWithZeroPoint * 2) < includedFrames);
    }

    [[nodiscard]] bool hasSoftWarning() const
    {
        return hasAnyZeroPointData() && !hasHardWarning() && (includedFrames > 0) &&
               (((framesWithScatter * 2) < includedFrames) || ((framesWithRms * 2) < includedFrames));
    }

    [[nodiscard]] QString warningText() const
    {
        if (hasHardWarning())
        {
            return QStringLiteral(
                       "Photometric weighting has limited photometric zero-point coverage (%1/%2 included frames). "
                       "Weights may be degraded or fall back to uniform behavior.")
                .arg(framesWithZeroPoint)
                .arg(includedFrames);
        }

        if (hasSoftWarning())
        {
            return QStringLiteral("Photometric weighting is using partial modifier coverage: zero-point scatter is "
                                  "available for %1/%2 frames "
                                  "and astrometric RMS for %3/%2. Missing modifiers are treated neutrally.")
                .arg(framesWithScatter)
                .arg(includedFrames)
                .arg(framesWithRms);
        }

        return {};
    }
};

[[nodiscard]] inline PhotometricCoverageSummary
summarizePhotometricCoverage(const QList<FrameQualityMetrics>& metricsList)
{
    PhotometricCoverageSummary summary;
    summary.includedFrames = metricsList.size();
    for (const auto& metrics : metricsList)
    {
        if (hasFiniteNonZeroMetric(metrics.photometric_zero_point))
        {
            ++summary.framesWithZeroPoint;
        }
        if (hasFinitePositiveMetric(metrics.zero_point_scatter))
        {
            ++summary.framesWithScatter;
        }
        if (hasFinitePositiveMetric(metrics.astrometric_rms_pixels))
        {
            ++summary.framesWithRms;
        }
    }
    return summary;
}

[[nodiscard]] inline PhotometricCoverageSummary
summarizePhotometricCoverage(const std::vector<FrameQualityMetrics>& metricsList)
{
    QList<FrameQualityMetrics> qtMetrics;
    qtMetrics.reserve(static_cast<qsizetype>(metricsList.size()));
    for (const auto& metrics : metricsList)
    {
        qtMetrics.append(metrics);
    }
    return summarizePhotometricCoverage(qtMetrics);
}

} // namespace ImageIntegration
