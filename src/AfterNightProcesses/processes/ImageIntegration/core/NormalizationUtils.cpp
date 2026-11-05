// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's normalization settings as the engine's normalization requests.

#include "stdafx.hpp"
#include "NormalizationUtils.h"

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <cmath>

namespace ImageIntegration
{

namespace
{

/// The constants for what no dialog offers: the sampling region's smallest side, how far past its cell it reaches, and
/// how many positions the residual-ratio scale is estimated from.
constexpr std::uint32_t kBackgroundMinRoiExtent = 32u;
constexpr double kBackgroundRoiOverlap = 0.5;
constexpr std::uint32_t kScaleSamples = 8192u;

[[nodiscard]] double clampedFinite(const double value, const double low, const double high, const double fallback)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

an::NormalizationMethod normalizationMethodFor(const GlobalNormalizationMethod method) noexcept
{
    switch (method)
    {
    case GlobalNormalizationMethod::Additive:
        return an::NormalizationMethod::Additive;
    case GlobalNormalizationMethod::Multiplicative:
        return an::NormalizationMethod::Multiplicative;
    case GlobalNormalizationMethod::AdditiveScaling:
        return an::NormalizationMethod::AdditiveScaling;
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return an::NormalizationMethod::MultiplicativeScaling;
    }
    return an::NormalizationMethod::AdditiveScaling;
}

an::ScaleEstimator scaleEstimatorFor(const ScaleEstimatorMethod method) noexcept
{
    return method == ScaleEstimatorMethod::IKSS ? an::ScaleEstimator::IterativeKSigma
                                                : an::ScaleEstimator::MedianAbsoluteDeviation;
}

an::LocalNormalizationRequest localNormalizationRequestFor(const LocalNormalizationSettings& settings)
{
    an::LocalNormalizationRequest request;
    request.gridSize = static_cast<std::uint32_t>(
        std::clamp(settings.gridSize, 1, static_cast<int>(an::kMaxLocalNormalizationGridSize)));

    const QString interpolation = normalizeNormalizationToken(settings.gridInterpolation);
    if (interpolation == QStringLiteral("bilinear"))
    {
        request.interpolation = an::LocalGridInterpolation::Bilinear;
    }
    else
    {
        if (!interpolation.isEmpty() && interpolation != QStringLiteral("bicubic"))
        {
            LOG_WARNING("ImageIntegration: unknown local normalization grid interpolation '{}', using Bicubic",
                        settings.gridInterpolation.toStdString());
        }
        request.interpolation = an::LocalGridInterpolation::Bicubic;
    }

    const QString estimator = normalizeNormalizationToken(settings.backgroundEstimator);
    if (estimator == QStringLiteral("percentile"))
    {
        request.estimator = an::LocalBackgroundEstimator::Percentile;
    }
    else if (estimator == QStringLiteral("sigma clipped mode"))
    {
        request.estimator = an::LocalBackgroundEstimator::SigmaClippedMode;
    }
    else
    {
        if (!estimator.isEmpty() && estimator != QStringLiteral("sigma clipped median"))
        {
            LOG_WARNING("ImageIntegration: unknown local normalization background estimator '{}', using Sigma "
                        "Clipped Median",
                        settings.backgroundEstimator.toStdString());
        }
        request.estimator = an::LocalBackgroundEstimator::SigmaClippedMedian;
    }

    request.backgroundPercentile = clampedFinite(settings.backgroundPercentile, 0.0, 1.0, 0.25);
    request.sigmaClip = clampedFinite(settings.sigmaClipK > 0.0 ? settings.sigmaClipK : 3.0,
                                      an::kMinLocalNormalizationSigmaClip, an::kMaxLocalNormalizationSigmaClip, 3.0);
    request.sigmaClipIterations = static_cast<std::uint32_t>(
        std::clamp(settings.sigmaClipIterations, 1, static_cast<int>(an::kMaxLocalNormalizationSigmaClipIterations)));
    request.backgroundMinRoiExtent = kBackgroundMinRoiExtent;
    request.backgroundRoiOverlap = kBackgroundRoiOverlap;
    request.backgroundSmoothingRadius = static_cast<std::uint32_t>((std::max)(0, settings.backgroundSmoothingRadius));
    request.lowClip = std::isfinite(settings.lowClip) ? settings.lowClip : 4.5e-5;
    request.highClip = std::isfinite(settings.highClip) ? settings.highClip : 0.85;
    if (request.lowClip >= request.highClip)
    {
        LOG_WARNING("ImageIntegration: local normalization clip window [{}, {}) is empty, using [4.5e-05, 0.85)",
                    request.lowClip, request.highClip);
        request.lowClip = 4.5e-5;
        request.highClip = 0.85;
    }

    const QString scaleMode = normalizeNormalizationToken(settings.scaleMode);
    if (scaleMode == QStringLiteral("per channel"))
    {
        request.scaleMode = an::LocalScaleMode::PerChannel;
    }
    else
    {
        if (!scaleMode.isEmpty() && scaleMode != QStringLiteral("linked rgb"))
        {
            LOG_WARNING("ImageIntegration: unknown local normalization scale mode '{}', using Linked RGB",
                        settings.scaleMode.toStdString());
        }
        request.scaleMode = an::LocalScaleMode::Linked;
    }
    request.scaleSamples = kScaleSamples;
    request.localScaleSmoothingRadius = static_cast<std::uint32_t>((std::max)(0, settings.localScaleSmoothingRadius));
    return request;
}

QJsonObject localNormalizationRequestJson(const an::LocalNormalizationRequest& request)
{
    QJsonObject object;
    object.insert(QStringLiteral("gridSize"), static_cast<qint64>(request.gridSize));
    object.insert(QStringLiteral("interpolation"),
                  QString::fromLatin1(an::localGridInterpolationName(request.interpolation)));
    object.insert(QStringLiteral("estimator"),
                  QString::fromLatin1(an::localBackgroundEstimatorName(request.estimator)));
    object.insert(QStringLiteral("backgroundPercentile"), request.backgroundPercentile);
    object.insert(QStringLiteral("sigmaClip"), request.sigmaClip);
    object.insert(QStringLiteral("sigmaClipIterations"), static_cast<qint64>(request.sigmaClipIterations));
    object.insert(QStringLiteral("backgroundMinRoiExtent"), static_cast<qint64>(request.backgroundMinRoiExtent));
    object.insert(QStringLiteral("backgroundRoiOverlap"), request.backgroundRoiOverlap);
    object.insert(QStringLiteral("backgroundSmoothingRadius"), static_cast<qint64>(request.backgroundSmoothingRadius));
    object.insert(QStringLiteral("lowClip"), request.lowClip);
    object.insert(QStringLiteral("highClip"), request.highClip);
    object.insert(QStringLiteral("scaleMode"), QString::fromLatin1(an::localScaleModeName(request.scaleMode)));
    object.insert(QStringLiteral("scaleSamples"), static_cast<qint64>(request.scaleSamples));
    object.insert(QStringLiteral("localScaleSmoothingRadius"), static_cast<qint64>(request.localScaleSmoothingRadius));
    return object;
}

} // namespace ImageIntegration
