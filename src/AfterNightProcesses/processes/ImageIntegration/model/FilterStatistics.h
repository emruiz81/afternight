// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file FilterStatistics.h
 * @brief Per-filter baseline statistics for quality metric normalization.
 *
 * This module provides filter-relative normalization of frame quality metrics.
 * Different optical filters (L, R, G, B, H-alpha, SII, OIII, etc.) inherently
 * produce different characteristic values for metrics like FWHM and SNR due to:
 *
 * - Wavelength-dependent optical performance (chromatic aberration)
 * - Narrowband filters having broader PSF than broadband
 * - Different sky background levels per filter
 * - Filter-specific transmission characteristics
 *
 * By computing per-filter baseline statistics (median and MAD), we can express
 * each frame's metrics as z-scores relative to its filter group, enabling fair
 * quality comparisons across filters.
 *
 * @par Example
 * An H-alpha frame with FWHM=4.5" might be excellent for H-alpha (z=-0.5),
 * while a Luminance frame with the same FWHM=4.5" might be poor (z=+1.2).
 *
 * @par Usage
 * @code
 * FilterStatisticsEngine engine;
 * auto stats = engine.computeStatistics(frames, getFilterFunc);
 * auto normalized = FilterStatisticsEngine::normalizeMetrics(
 *     absoluteMetrics, stats["Ha"]);
 * @endcode
 *
 * @see FrameQualityMetrics, FrameQualityDashboard
 */

#include "FrameId.h"
#include "FrameQuality.h"

#include <QString>
#include <functional>
#include <map>
#include <optional>
#include <vector>

/**
 * @brief Per-filter baseline statistics for metric normalization.
 *
 * Contains the median and MAD (Median Absolute Deviation) for each quality
 * metric within a single filter group. MAD is used instead of standard
 * deviation for robustness against outliers.
 *
 * The MAD is scaled by 1.4826 to be consistent with standard deviation
 * for normally distributed data: σ ≈ 1.4826 × MAD
 */
struct FilterStatistics
{
    QString filter; ///< Filter name (e.g., "L", "Ha", "Red")
    int frameCount = 0; ///< Number of frames used to compute statistics

    // FWHM statistics (in pixels or arcseconds, depending on input)
    double fwhmMedian = 0.0; ///< Median FWHM for this filter
    double fwhmMad = 0.0; ///< MAD of FWHM (scaled to σ equivalent)

    // SNR statistics
    double snrMedian = 0.0; ///< Median SNR for this filter
    double snrMad = 0.0; ///< MAD of SNR (scaled to σ equivalent)

    // Eccentricity statistics (range [0, 1])
    double eccMedian = 0.0; ///< Median eccentricity for this filter
    double eccMad = 0.0; ///< MAD of eccentricity (scaled to σ equivalent)

    // Star count statistics
    double starsMedian = 0.0; ///< Median star count for this filter
    double starsMad = 0.0; ///< MAD of star count (scaled to σ equivalent)

    // Photometric zero point statistics
    double zeroPointMedian = 0.0; ///< Median photometric zero point
    double zeroPointMad = 0.0; ///< MAD of photometric zero point

    // Zero-point scatter statistics
    double zeroPointScatterMedian = 0.0; ///< Median zero-point scatter
    double zeroPointScatterMad = 0.0; ///< MAD of zero-point scatter

    // Astrometric RMS statistics
    double astrometricRmsMedian = 0.0; ///< Median astrometric RMS (arcsec)
    double astrometricRmsMad = 0.0; ///< MAD of astrometric RMS

    // Limiting magnitude statistics
    double limitingMagnitudeMedian = 0.0; ///< Median limiting magnitude
    double limitingMagnitudeMad = 0.0; ///< MAD of limiting magnitude

    // Catalog match statistics
    double catalogMatchedMedian = 0.0; ///< Median catalog matched stars
    double catalogMatchedMad = 0.0; ///< MAD of catalog matched stars

    /**
     * @brief Check if statistics are valid for normalization.
     * @return true if enough frames and non-zero MAD values exist.
     */
    [[nodiscard]] bool isValid() const;
};

/**
 * @brief Normalized (z-score) quality metrics relative to filter baseline.
 *
 * Each z-score indicates how many standard deviations the metric is from
 * the filter's median:
 * - z = 0 means exactly at the filter median
 * - z < 0 means better than median (for FWHM, eccentricity)
 * - z > 0 means better than median (for SNR, star count)
 *
 * Note: The sign convention is chosen so that POSITIVE z-scores always
 * indicate BETTER quality, regardless of metric type. This means:
 * - FWHM z-score is inverted: lower FWHM → positive z
 * - Eccentricity z-score is inverted: lower eccentricity → positive z
 * - SNR z-score is direct: higher SNR → positive z
 * - Stars z-score is direct: more stars → positive z
 */
struct AFTERNIGHTPROCESSES_API NormalizedQualityMetrics
{
    double fwhmZ = 0.0; ///< FWHM z-score (positive = sharper than median)
    double snrZ = 0.0; ///< SNR z-score (positive = better SNR than median)
    double eccZ = 0.0; ///< Eccentricity z-score (positive = rounder than median)
    double starsZ = 0.0; ///< Star count z-score (positive = more stars than median)
    double zeroPointZ = 0.0; ///< Zero point z-score (positive = better transparency)
    double zeroPointScatterZ = 0.0; ///< Zero-point scatter z-score (positive = lower scatter)
    double astrometricRmsZ = 0.0; ///< Astrometric RMS z-score (positive = lower residuals)
    double limitingMagnitudeZ = 0.0; ///< Limiting magnitude z-score (positive = deeper)
    double catalogMatchedZ = 0.0; ///< Catalog-matched-stars z-score (positive = stronger solve)

    /**
     * @brief Combined quality z-score (weighted sum of individual z-scores).
     *
     * Higher values indicate better overall quality relative to filter peers.
     */
    [[nodiscard]] double combinedZ() const;
};

/**
 * @brief Engine for computing per-filter statistics and normalizing metrics.
 *
 * This class provides two main functions:
 * 1. computeStatistics(): Analyze a set of frames to build per-filter baselines
 * 2. normalizeMetrics(): Convert absolute metrics to filter-relative z-scores
 *
 * @par Thread Safety
 * All methods are const and thread-safe. The engine holds no mutable state.
 */
class AFTERNIGHTPROCESSES_API FilterStatisticsEngine
{
public:
    using FilterStatsMap = std::map<QString, FilterStatistics>;
    using FilterLookupFunc = std::function<QString(const FrameId&)>;

    /**
     * @brief Compute per-filter baseline statistics from a set of frames.
     *
     * Groups frames by filter and computes median/MAD for each quality metric.
     * Only frames with succeeded=true are included in the statistics.
     *
     * @param frames Vector of (FrameId, FrameQualityMetrics) pairs.
     * @param getFilter Function to retrieve filter name from FrameId.
     * @return Map of filter name to FilterStatistics.
     */
    [[nodiscard]] FilterStatsMap computeStatistics(const std::vector<std::pair<FrameId, FrameQualityMetrics>>& frames,
                                                   const FilterLookupFunc& getFilter) const;

    /**
     * @brief Compute baseline statistics for a single filter from its metrics.
     *
     * This is a simpler overload for when frames are already grouped by filter.
     *
     * @param filterName The name of the filter.
     * @param metrics Vector of quality metrics for this filter.
     * @return FilterStatistics for this filter group.
     */
    [[nodiscard]] static FilterStatistics computeStatistics(const QString& filterName,
                                                            const std::vector<FrameQualityMetrics>& metrics);

    /**
     * @brief Normalize absolute metrics to filter-relative z-scores.
     *
     * Converts each metric to a z-score using the formula:
     *   z = (value - median) / mad
     *
     * For FWHM and eccentricity, the sign is inverted so that positive
     * z-scores always indicate better quality.
     *
     * @param absolute The absolute quality metrics for a single frame.
     * @param filterStats The baseline statistics for the frame's filter.
     * @return Normalized z-score metrics.
     */
    [[nodiscard]] static NormalizedQualityMetrics normalizeMetrics(const FrameQualityMetrics& absolute,
                                                                   const FilterStatistics& filterStats);

    /**
     * @brief Compute quality score using filter-relative normalization.
     *
     * This is the filter-aware version of frameQualityScore(). When filter
     * statistics are available, the score uses z-scores for fair comparison.
     * Falls back to absolute scoring when statistics are unavailable.
     *
     * @param metrics Absolute quality metrics.
     * @param filterStats Optional filter baseline statistics.
     * @return Quality score (higher = better).
     */
    [[nodiscard]] static double computeQualityScore(const FrameQualityMetrics& metrics,
                                                    const std::optional<FilterStatistics>& filterStats = std::nullopt);

private:
    /**
     * @brief Compute statistics from a pre-collected vector of metric pointers.
     *
     * Shared implementation used by both computeStatistics() overloads.
     */
    [[nodiscard]] static FilterStatistics
    computeStatsFromPointers(const QString& filterName, const std::vector<const FrameQualityMetrics*>& metricsVec);
};
