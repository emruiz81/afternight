// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FilterStatistics.h"
#include "processes/ImageIntegration/core/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{

/// Minimum MAD value to avoid division by zero
constexpr double MIN_MAD = 1e-9;

/// Minimum number of frames needed for meaningful statistics
constexpr int MIN_FRAMES_FOR_STATS = 1;

} // namespace

bool FilterStatistics::isValid() const
{
    return frameCount >= MIN_FRAMES_FOR_STATS && fwhmMad > MIN_MAD && snrMad > MIN_MAD;
}

double NormalizedQualityMetrics::combinedZ() const
{
    // Weighted combination of z-scores
    // All z-scores are already oriented so positive = better
    // Weights reflect relative importance for image quality
    constexpr double FWHM_WEIGHT = 2.0; // Sharpness is very important
    constexpr double SNR_WEIGHT = 1.5; // Signal quality matters
    constexpr double ECC_WEIGHT = 1.0; // Roundness is important
    constexpr double STARS_WEIGHT = 1.0; // Star count indicates conditions
    constexpr double ZERO_POINT_WEIGHT = 2.0; // Transparency is now a stronger discriminator
    constexpr double ZERO_POINT_SCATTER_WEIGHT = 1.5; // Photometric consistency penalty
    constexpr double ASTRO_RMS_WEIGHT = 2.0; // Tracking / solve residuals matter strongly
    constexpr double LIMITING_MAG_WEIGHT = 0.5; // Depth proxy
    constexpr double CATALOG_MATCH_WEIGHT = 0.75; // Solve confidence

    return fwhmZ * FWHM_WEIGHT + snrZ * SNR_WEIGHT + eccZ * ECC_WEIGHT + starsZ * STARS_WEIGHT +
           zeroPointZ * ZERO_POINT_WEIGHT + zeroPointScatterZ * ZERO_POINT_SCATTER_WEIGHT +
           astrometricRmsZ * ASTRO_RMS_WEIGHT + limitingMagnitudeZ * LIMITING_MAG_WEIGHT +
           catalogMatchedZ * CATALOG_MATCH_WEIGHT;
}

FilterStatisticsEngine::FilterStatsMap
FilterStatisticsEngine::computeStatistics(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& frames,
                                          const FilterLookupFunc& getFilter) const
{
    // Group frames by filter
    std::map<QString, std::vector<const FrameQualityMetrics*>> filterGroups;

    for (const auto& [frameId, metrics] : frames)
    {
        if (!metrics.succeeded)
            continue;

        const QString filter = getFilter(frameId);
        filterGroups[filter].push_back(&metrics);
    }

    FilterStatsMap result;
    for (auto& [filter, metricsVec] : filterGroups)
    {
        result[filter] = computeStatsFromPointers(filter, metricsVec);
    }
    return result;
}

FilterStatistics FilterStatisticsEngine::computeStatistics(const QString& filterName,
                                                           const std::vector<FrameQualityMetrics>& metrics)
{
    std::vector<const FrameQualityMetrics*> validMetrics;
    validMetrics.reserve(metrics.size());
    for (const auto& m : metrics)
    {
        if (m.succeeded)
        {
            validMetrics.push_back(&m);
        }
    }
    return computeStatsFromPointers(filterName, validMetrics);
}

FilterStatistics
FilterStatisticsEngine::computeStatsFromPointers(const QString& filterName,
                                                 const std::vector<const FrameQualityMetrics*>& metricsVec)
{
    FilterStatistics stats;
    stats.filter = filterName;
    stats.frameCount = static_cast<int>(metricsVec.size());

    if (stats.frameCount < MIN_FRAMES_FOR_STATS)
    {
        return stats;
    }

    // Collect values for each metric
    std::vector<double> fwhmValues;
    std::vector<double> snrValues;
    std::vector<double> eccValues;
    std::vector<double> starsValues;
    std::vector<double> zeroPointValues;
    std::vector<double> zeroPointScatterValues;
    std::vector<double> astrometricRmsValues;
    std::vector<double> limitingMagnitudeValues;
    std::vector<double> catalogMatchedValues;

    fwhmValues.reserve(metricsVec.size());
    snrValues.reserve(metricsVec.size());
    eccValues.reserve(metricsVec.size());
    starsValues.reserve(metricsVec.size());
    zeroPointValues.reserve(metricsVec.size());
    zeroPointScatterValues.reserve(metricsVec.size());
    astrometricRmsValues.reserve(metricsVec.size());
    limitingMagnitudeValues.reserve(metricsVec.size());
    catalogMatchedValues.reserve(metricsVec.size());

    for (const auto* m : metricsVec)
    {
        if (m->fwhm > 0.0)
            fwhmValues.push_back(m->fwhm);
        if (m->snr > 0.0)
            snrValues.push_back(m->snr);
        eccValues.push_back(m->eccentricity);
        starsValues.push_back(static_cast<double>(m->stars));
        if (std::isfinite(m->photometric_zero_point) && std::abs(m->photometric_zero_point) > MIN_MAD)
            zeroPointValues.push_back(m->photometric_zero_point);
        if (std::isfinite(m->zero_point_scatter) && m->zero_point_scatter > 0.0)
            zeroPointScatterValues.push_back(m->zero_point_scatter);
        if (std::isfinite(m->astrometric_rms_arcsec) && m->astrometric_rms_arcsec > 0.0)
            astrometricRmsValues.push_back(m->astrometric_rms_arcsec);
        if (std::isfinite(m->limiting_magnitude) && m->limiting_magnitude > 0.0)
            limitingMagnitudeValues.push_back(m->limiting_magnitude);
        if (m->catalog_matched_stars > 0)
            catalogMatchedValues.push_back(static_cast<double>(m->catalog_matched_stars));
    }

    // Compute median and MAD for each metric.
    // MAD will be 0 for a single frame; isValid() will return false
    // for statistical purposes, but we still store the median values.
    if (!fwhmValues.empty())
    {
        stats.fwhmMedian = ImageIntegration::medianOf(fwhmValues);
        stats.fwhmMad = ImageIntegration::scaledMadOf(fwhmValues, stats.fwhmMedian);
    }
    if (!snrValues.empty())
    {
        stats.snrMedian = ImageIntegration::medianOf(snrValues);
        stats.snrMad = ImageIntegration::scaledMadOf(snrValues, stats.snrMedian);
    }
    if (!eccValues.empty())
    {
        stats.eccMedian = ImageIntegration::medianOf(eccValues);
        stats.eccMad = ImageIntegration::scaledMadOf(eccValues, stats.eccMedian);
    }
    if (!starsValues.empty())
    {
        stats.starsMedian = ImageIntegration::medianOf(starsValues);
        stats.starsMad = ImageIntegration::scaledMadOf(starsValues, stats.starsMedian);
    }
    if (!zeroPointValues.empty())
    {
        stats.zeroPointMedian = ImageIntegration::medianOf(zeroPointValues);
        stats.zeroPointMad = ImageIntegration::scaledMadOf(zeroPointValues, stats.zeroPointMedian);
    }
    if (!zeroPointScatterValues.empty())
    {
        stats.zeroPointScatterMedian = ImageIntegration::medianOf(zeroPointScatterValues);
        stats.zeroPointScatterMad = ImageIntegration::scaledMadOf(zeroPointScatterValues, stats.zeroPointScatterMedian);
    }
    if (!astrometricRmsValues.empty())
    {
        stats.astrometricRmsMedian = ImageIntegration::medianOf(astrometricRmsValues);
        stats.astrometricRmsMad = ImageIntegration::scaledMadOf(astrometricRmsValues, stats.astrometricRmsMedian);
    }
    if (!limitingMagnitudeValues.empty())
    {
        stats.limitingMagnitudeMedian = ImageIntegration::medianOf(limitingMagnitudeValues);
        stats.limitingMagnitudeMad =
            ImageIntegration::scaledMadOf(limitingMagnitudeValues, stats.limitingMagnitudeMedian);
    }
    if (!catalogMatchedValues.empty())
    {
        stats.catalogMatchedMedian = ImageIntegration::medianOf(catalogMatchedValues);
        stats.catalogMatchedMad = ImageIntegration::scaledMadOf(catalogMatchedValues, stats.catalogMatchedMedian);
    }

    return stats;
}

NormalizedQualityMetrics FilterStatisticsEngine::normalizeMetrics(const FrameQualityMetrics& absolute,
                                                                  const FilterStatistics& filterStats)
{
    NormalizedQualityMetrics normalized;

    // Compute z-scores with sign convention: positive = better quality
    // FWHM: lower is better, so negate the z-score
    if (filterStats.fwhmMad > MIN_MAD && absolute.fwhm > 0.0)
    {
        normalized.fwhmZ = -(absolute.fwhm - filterStats.fwhmMedian) / filterStats.fwhmMad;
    }

    // SNR: higher is better, direct z-score
    if (filterStats.snrMad > MIN_MAD && absolute.snr > 0.0)
    {
        normalized.snrZ = (absolute.snr - filterStats.snrMedian) / filterStats.snrMad;
    }

    // Eccentricity: lower is better, so negate the z-score
    if (filterStats.eccMad > MIN_MAD)
    {
        normalized.eccZ = -(absolute.eccentricity - filterStats.eccMedian) / filterStats.eccMad;
    }

    // Stars: higher is better, direct z-score
    if (filterStats.starsMad > MIN_MAD)
    {
        normalized.starsZ = (static_cast<double>(absolute.stars) - filterStats.starsMedian) / filterStats.starsMad;
    }
    if (filterStats.zeroPointMad > MIN_MAD && std::isfinite(absolute.photometric_zero_point) &&
        std::abs(absolute.photometric_zero_point) > MIN_MAD)
    {
        normalized.zeroPointZ =
            (absolute.photometric_zero_point - filterStats.zeroPointMedian) / filterStats.zeroPointMad;
    }
    if (filterStats.astrometricRmsMad > MIN_MAD && absolute.astrometric_rms_arcsec > 0.0)
    {
        normalized.astrometricRmsZ =
            -(absolute.astrometric_rms_arcsec - filterStats.astrometricRmsMedian) / filterStats.astrometricRmsMad;
    }
    if (filterStats.zeroPointScatterMad > MIN_MAD && absolute.zero_point_scatter > 0.0)
    {
        // Lower scatter is better, so negate the z-score
        normalized.zeroPointScatterZ =
            -(absolute.zero_point_scatter - filterStats.zeroPointScatterMedian) / filterStats.zeroPointScatterMad;
    }
    if (filterStats.limitingMagnitudeMad > MIN_MAD && absolute.limiting_magnitude > 0.0)
    {
        // Higher limiting magnitude is better (can detect fainter stars), direct z-score
        normalized.limitingMagnitudeZ =
            (absolute.limiting_magnitude - filterStats.limitingMagnitudeMedian) / filterStats.limitingMagnitudeMad;
    }
    if (filterStats.catalogMatchedMad > MIN_MAD && absolute.catalog_matched_stars > 0)
    {
        normalized.catalogMatchedZ =
            (static_cast<double>(absolute.catalog_matched_stars) - filterStats.catalogMatchedMedian) /
            filterStats.catalogMatchedMad;
    }

    return normalized;
}

double FilterStatisticsEngine::computeQualityScore(const FrameQualityMetrics& metrics,
                                                   const std::optional<FilterStatistics>& filterStats)
{
    if (!metrics.succeeded)
    {
        return -std::numeric_limits<double>::infinity();
    }

    const auto clampScore = [](double value) {
        // The UI/UX expects a stable, comparable score (roughly "0-100").
        // Filter-relative z-scores can otherwise create extreme values when a
        // filter group has very low variance (tiny MAD) or the frame is an outlier.
        return std::clamp(value, 0.0, 100.0);
    };

    // If we have valid filter statistics, use z-score based scoring
    if (filterStats && filterStats->isValid())
    {
        const auto normalized = normalizeMetrics(metrics, *filterStats);

        // Z-score based scoring:
        // Each z-score is already scaled so positive = better
        // We weight and sum them with a baseline offset to keep scores positive
        constexpr double BASELINE = 50.0; // Median quality frame scores ~50
        constexpr double SCALE = 10.0; // Each σ contributes ~10 points

        return clampScore(BASELINE + normalized.combinedZ() * SCALE);
    }

    // Fallback to absolute scoring (original formula from FrameQuality.cpp).
    // This is used when we don't have enough frames to compute filter statistics.
    // Absolute scores are not clamped — their range differs from z-score scoring
    // and clamping would discard meaningful differentiation between frames.
    return frameQualityScore(metrics);
}
