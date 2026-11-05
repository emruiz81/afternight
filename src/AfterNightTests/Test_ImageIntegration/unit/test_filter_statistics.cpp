// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_filter_statistics.cpp
 * @brief Unit tests for FilterStatistics and FilterStatisticsEngine
 *
 * Tests the filter-relative metric normalization system that computes
 * per-filter baselines and z-scores for frame quality comparison.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/FrameQuality.h"

namespace
{
FrameQualityMetrics makeMetrics(int stars, double fwhm, double snr, double eccentricity, bool succeeded = true)
{
    FrameQualityMetrics m;
    m.stars = stars;
    m.fwhm = fwhm;
    m.snr = snr;
    m.eccentricity = eccentricity;
    m.succeeded = succeeded;
    return m;
}
} // namespace

// ============================================================================
// FilterStatisticsEngine::computeStatistics() Tests
// ============================================================================

TEST(FilterStatisticsCompute, EmptyInputReturnsZeroFrameCount)
{
    std::vector<FrameQualityMetrics> empty;
    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("Ha"), empty);

    EXPECT_EQ(stats.frameCount, 0);
    EXPECT_EQ(stats.filter, QStringLiteral("Ha"));
}

TEST(FilterStatisticsCompute, SingleFrameReturnsFrameValuesAsMedian)
{
    std::vector<FrameQualityMetrics> frames = {makeMetrics(500, 2.5, 30.0, 0.2)};

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("OIII"), frames);

    EXPECT_EQ(stats.frameCount, 1);
    EXPECT_DOUBLE_EQ(stats.starsMedian, 500.0);
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 2.5);
    EXPECT_DOUBLE_EQ(stats.snrMedian, 30.0);
    EXPECT_DOUBLE_EQ(stats.eccMedian, 0.2);
    // MAD should be 0 or minimal for single frame
    EXPECT_LE(stats.starsMad, 1e-6);
    EXPECT_LE(stats.fwhmMad, 1e-6);
    EXPECT_LE(stats.snrMad, 1e-6);
    EXPECT_LE(stats.eccMad, 1e-6);
}

TEST(FilterStatisticsCompute, OddNumberOfFramesComputesCorrectMedian)
{
    // 5 frames with increasing FWHM: 1.0, 2.0, 3.0, 4.0, 5.0
    // Median should be 3.0
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 5.0, 30.0, 0.2), // Shuffle order
        makeMetrics(500, 2.0, 30.0, 0.2), makeMetrics(500, 3.0, 30.0, 0.2),
        makeMetrics(500, 1.0, 30.0, 0.2), makeMetrics(500, 4.0, 30.0, 0.2),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("L"), frames);

    EXPECT_EQ(stats.frameCount, 5);
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 3.0);
}

TEST(FilterStatisticsCompute, EvenNumberOfFramesComputesCorrectMedian)
{
    // 4 frames with FWHM: 1.0, 2.0, 3.0, 4.0
    // Median should be 2.5 (average of middle two)
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 4.0, 30.0, 0.2),
        makeMetrics(500, 1.0, 30.0, 0.2),
        makeMetrics(500, 3.0, 30.0, 0.2),
        makeMetrics(500, 2.0, 30.0, 0.2),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("R"), frames);

    EXPECT_EQ(stats.frameCount, 4);
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 2.5);
}

TEST(FilterStatisticsCompute, MadComputedCorrectly)
{
    // FWHM values: 1.0, 2.0, 3.0, 4.0, 5.0
    // Median = 3.0
    // Deviations from median: |1-3|=2, |2-3|=1, |3-3|=0, |4-3|=1, |5-3|=2
    // MAD = median of {0, 1, 1, 2, 2} = 1.0
    // Scaled MAD = 1.0 * 1.4826 = 1.4826
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 1.0, 30.0, 0.2), makeMetrics(500, 2.0, 30.0, 0.2), makeMetrics(500, 3.0, 30.0, 0.2),
        makeMetrics(500, 4.0, 30.0, 0.2), makeMetrics(500, 5.0, 30.0, 0.2),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("G"), frames);

    EXPECT_NEAR(stats.fwhmMad, 1.4826, 0.001);
}

TEST(FilterStatisticsCompute, IdenticalValuesGiveZeroMad)
{
    // All frames have identical FWHM
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 2.5, 30.0, 0.2),
        makeMetrics(500, 2.5, 30.0, 0.2),
        makeMetrics(500, 2.5, 30.0, 0.2),
        makeMetrics(500, 2.5, 30.0, 0.2),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("B"), frames);

    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 2.5);
    // MAD should be 0 (or very close) for identical values
    EXPECT_LE(stats.fwhmMad, 1e-6);
}

TEST(FilterStatisticsCompute, IgnoresFailedFrames)
{
    // Only the failed frame has outlier FWHM - should be ignored
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 2.0, 30.0, 0.2, true),
        makeMetrics(500, 2.5, 30.0, 0.2, true),
        makeMetrics(500, 3.0, 30.0, 0.2, true),
        makeMetrics(500, 100.0, 30.0, 0.2, false), // Failed - should be ignored
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("Ha"), frames);

    EXPECT_EQ(stats.frameCount, 3); // Only successful frames counted
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 2.5);
}

TEST(FilterStatisticsCompute, TracksExtendedMetricMedians)
{
    auto a = makeMetrics(500, 2.5, 30.0, 0.2);
    auto b = makeMetrics(520, 2.6, 31.0, 0.2);
    auto c = makeMetrics(540, 2.7, 32.0, 0.2);
    a.photometric_zero_point = 24.0;
    b.photometric_zero_point = 25.0;
    c.photometric_zero_point = 26.0;
    a.astrometric_rms_arcsec = 0.4;
    b.astrometric_rms_arcsec = 0.5;
    c.astrometric_rms_arcsec = 0.6;
    a.catalog_matched_stars = 90;
    b.catalog_matched_stars = 100;
    c.catalog_matched_stars = 110;

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("L"), {a, b, c});

    EXPECT_DOUBLE_EQ(stats.zeroPointMedian, 25.0);
    EXPECT_DOUBLE_EQ(stats.astrometricRmsMedian, 0.5);
    EXPECT_DOUBLE_EQ(stats.catalogMatchedMedian, 100.0);
}

// ============================================================================
// FilterStatisticsEngine::normalizeMetrics() Tests
// ============================================================================

TEST(NormalizeMetrics, ReturnsZeroZScoresWhenNoStats)
{
    FilterStatistics emptyStats;
    emptyStats.frameCount = 0;

    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 2.5, 30.0, 0.2), emptyStats);

    EXPECT_DOUBLE_EQ(normalized.fwhmZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.snrZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.eccZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.starsZ, 0.0);
}

TEST(NormalizeMetrics, PositiveZScoreMeansBetterThanMedian)
{
    // Create stats where median FWHM is 3.0
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // Frame with FWHM = 2.0 (better than median 3.0)
    // z = (2.0 - 3.0) / 1.0 = -1.0, but sign flipped because lower is better
    // So zFwhm should be +1.0
    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 2.0, 30.0, 0.2), stats);

    EXPECT_GT(normalized.fwhmZ, 0.0); // Better FWHM = positive z-score
}

TEST(NormalizeMetrics, NegativeZScoreMeansWorseThanMedian)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // Frame with FWHM = 4.0 (worse than median 3.0)
    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 4.0, 30.0, 0.2), stats);

    EXPECT_LT(normalized.fwhmZ, 0.0); // Worse FWHM = negative z-score
}

TEST(NormalizeMetrics, HigherSnrGivesPositiveZScore)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // SNR = 50 (better than median 30)
    // z = (50 - 30) / 10 = +2.0 (higher is better, no sign flip)
    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 3.0, 50.0, 0.2), stats);

    EXPECT_NEAR(normalized.snrZ, 2.0, 0.001);
}

TEST(NormalizeMetrics, LowerEccentricityGivesPositiveZScore)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.3;
    stats.eccMad = 0.1;

    // Eccentricity = 0.1 (better than median 0.3)
    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 3.0, 30.0, 0.1), stats);

    EXPECT_GT(normalized.eccZ, 0.0); // Lower ecc = positive z
}

TEST(NormalizeMetrics, HandlesZeroMadGracefully)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 0.0; // All frames had identical FWHM
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // Should not produce NaN or Inf
    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 3.5, 30.0, 0.2), stats);

    EXPECT_TRUE(std::isfinite(normalized.fwhmZ));
    // With zero MAD, even small deviation could produce large z-score
    // but it should still be finite
}

TEST(NormalizeMetrics, ExtendedMetricsFollowPositiveIsBetterConvention)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.zeroPointMedian = 25.0;
    stats.zeroPointMad = 0.5;
    stats.astrometricRmsMedian = 1.0;
    stats.astrometricRmsMad = 0.25;
    stats.catalogMatchedMedian = 100.0;
    stats.catalogMatchedMad = 10.0;

    auto metrics = makeMetrics(500, 3.0, 30.0, 0.2);
    metrics.photometric_zero_point = 26.0;
    metrics.astrometric_rms_arcsec = 0.5;
    metrics.catalog_matched_stars = 120;

    const auto normalized = FilterStatisticsEngine::normalizeMetrics(metrics, stats);

    EXPECT_GT(normalized.zeroPointZ, 0.0);
    EXPECT_GT(normalized.astrometricRmsZ, 0.0);
    EXPECT_GT(normalized.catalogMatchedZ, 0.0);
}

TEST(NormalizeMetrics, MissingPhotometricMetadataScoresNeutrally)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;
    stats.zeroPointMedian = 24.0;
    stats.zeroPointMad = 0.5;
    stats.zeroPointScatterMedian = 0.1;
    stats.zeroPointScatterMad = 0.02;
    stats.astrometricRmsMedian = 0.8;
    stats.astrometricRmsMad = 0.1;
    stats.limitingMagnitudeMedian = 13.0;
    stats.limitingMagnitudeMad = 0.5;
    stats.catalogMatchedMedian = 120.0;
    stats.catalogMatchedMad = 10.0;

    const auto normalized = FilterStatisticsEngine::normalizeMetrics(makeMetrics(500, 3.0, 30.0, 0.2), stats);

    EXPECT_DOUBLE_EQ(normalized.zeroPointZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.zeroPointScatterZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.astrometricRmsZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.limitingMagnitudeZ, 0.0);
    EXPECT_DOUBLE_EQ(normalized.catalogMatchedZ, 0.0);
}

TEST(NormalizedQualityMetrics, CombinedZUsesRebalancedPhotometricWeights)
{
    NormalizedQualityMetrics metrics;
    metrics.fwhmZ = 1.0;
    metrics.snrZ = 1.0;
    metrics.eccZ = 1.0;
    metrics.starsZ = 1.0;
    metrics.zeroPointZ = 1.0;
    metrics.zeroPointScatterZ = 1.0;
    metrics.astrometricRmsZ = 1.0;
    metrics.limitingMagnitudeZ = 1.0;
    metrics.catalogMatchedZ = 1.0;

    EXPECT_NEAR(metrics.combinedZ(), 12.25, 1e-9);
}

// ============================================================================
// FilterStatisticsEngine::computeQualityScore() Tests
// ============================================================================

TEST(ComputeQualityScore, BaselineIs50ForMedianFrame)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // Frame exactly at median on all metrics
    const double score = FilterStatisticsEngine::computeQualityScore(makeMetrics(500, 3.0, 30.0, 0.2), stats);

    // Score should be close to 50 (the baseline)
    EXPECT_NEAR(score, 50.0, 1.0);
}

TEST(ComputeQualityScore, BetterFrameScoresHigher)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    // Median frame
    const double medianScore = FilterStatisticsEngine::computeQualityScore(makeMetrics(500, 3.0, 30.0, 0.2), stats);

    // Excellent frame: lower FWHM, higher SNR, more stars, lower eccentricity
    const double excellentScore = FilterStatisticsEngine::computeQualityScore(makeMetrics(800, 1.5, 60.0, 0.1), stats);

    EXPECT_GT(excellentScore, medianScore);
    EXPECT_GE(excellentScore, 0.0);
    EXPECT_LE(excellentScore, 100.0);
}

TEST(ComputeQualityScore, WorseFrameScoresLower)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    const double medianScore = FilterStatisticsEngine::computeQualityScore(makeMetrics(500, 3.0, 30.0, 0.2), stats);

    // Poor frame: higher FWHM, lower SNR, fewer stars, higher eccentricity
    const double poorScore = FilterStatisticsEngine::computeQualityScore(makeMetrics(200, 5.0, 15.0, 0.5), stats);

    EXPECT_LT(poorScore, medianScore);
    EXPECT_GE(poorScore, 0.0);
    EXPECT_LE(poorScore, 100.0);
}

TEST(ComputeQualityScore, ExtendedMetricsAffectNormalizedScore)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;
    stats.zeroPointMedian = 25.0;
    stats.zeroPointMad = 0.5;
    stats.astrometricRmsMedian = 1.0;
    stats.astrometricRmsMad = 0.25;
    stats.catalogMatchedMedian = 100.0;
    stats.catalogMatchedMad = 10.0;

    auto baseline = makeMetrics(500, 3.0, 30.0, 0.2);
    baseline.photometric_zero_point = 25.0;
    baseline.astrometric_rms_arcsec = 1.0;
    baseline.catalog_matched_stars = 100;

    auto improved = baseline;
    improved.photometric_zero_point = 26.0;
    improved.astrometric_rms_arcsec = 0.5;
    improved.catalog_matched_stars = 120;

    EXPECT_GT(FilterStatisticsEngine::computeQualityScore(improved, stats),
              FilterStatisticsEngine::computeQualityScore(baseline, stats));
}

TEST(ComputeQualityScore, MissingPhotometricMetadataDoesNotGetArtificiallyPenalized)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;
    stats.zeroPointMedian = 24.0;
    stats.zeroPointMad = 0.5;
    stats.catalogMatchedMedian = 100.0;
    stats.catalogMatchedMad = 10.0;

    auto improved = makeMetrics(500, 3.0, 30.0, 0.2);
    improved.photometric_zero_point = 24.5;
    improved.catalog_matched_stars = 110;

    const auto missing_photometric = makeMetrics(500, 3.0, 30.0, 0.2);

    EXPECT_NEAR(FilterStatisticsEngine::computeQualityScore(missing_photometric, stats), 50.0, 1e-9);
    EXPECT_LT(FilterStatisticsEngine::computeQualityScore(missing_photometric, stats),
              FilterStatisticsEngine::computeQualityScore(improved, stats));
}

TEST(ComputeQualityScore, FailedFrameReturnsNegativeInfinity)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    const double score = FilterStatisticsEngine::computeQualityScore(makeMetrics(500, 3.0, 30.0, 0.2, false), stats);

    EXPECT_EQ(score, -std::numeric_limits<double>::infinity());
}

TEST(ComputeQualityScore, FallsBackToAbsoluteWhenNoStats)
{
    FilterStatistics emptyStats;
    emptyStats.frameCount = 0;

    // Should still produce a reasonable finite score using absolute formula
    const double score = FilterStatisticsEngine::computeQualityScore(makeMetrics(500, 3.0, 30.0, 0.2), emptyStats);

    EXPECT_TRUE(std::isfinite(score));
    EXPECT_GT(score, 0.0);
}

// ============================================================================
// Integration with frameQualityScore() overload
// ============================================================================

TEST(FrameQualityScoreWithFilter, UsesFilterStatsWhenAvailable)
{
    FilterStatistics stats;
    stats.frameCount = 10;
    stats.fwhmMedian = 3.0;
    stats.fwhmMad = 1.0;
    stats.snrMedian = 30.0;
    stats.snrMad = 10.0;
    stats.starsMedian = 500.0;
    stats.starsMad = 100.0;
    stats.eccMedian = 0.2;
    stats.eccMad = 0.1;

    FrameQualityMetrics metrics = makeMetrics(500, 3.0, 30.0, 0.2);

    // Absolute score
    const double absoluteScore = frameQualityScore(metrics);

    // Filter-relative score
    const double filterScore = frameQualityScore(metrics, stats);

    // Both should be finite, but may differ
    EXPECT_TRUE(std::isfinite(absoluteScore));
    EXPECT_TRUE(std::isfinite(filterScore));

    // Filter score for median frame should be around 50
    EXPECT_NEAR(filterScore, 50.0, 2.0);
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

TEST(FilterStatisticsEdgeCases, HandlesVeryLargeValues)
{
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(100000, 0.1, 10000.0, 0.01),
        makeMetrics(100000, 0.2, 10000.0, 0.02),
        makeMetrics(100000, 0.3, 10000.0, 0.03),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("X"), frames);

    EXPECT_TRUE(std::isfinite(stats.starsMedian));
    EXPECT_TRUE(std::isfinite(stats.fwhmMedian));
    EXPECT_TRUE(std::isfinite(stats.snrMedian));
    EXPECT_TRUE(std::isfinite(stats.eccMedian));
}

TEST(FilterStatisticsEdgeCases, HandlesVerySmallValues)
{
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(1, 0.001, 0.01, 0.001),
        makeMetrics(2, 0.002, 0.02, 0.002),
        makeMetrics(3, 0.003, 0.03, 0.003),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("Y"), frames);

    EXPECT_TRUE(std::isfinite(stats.starsMedian));
    EXPECT_TRUE(std::isfinite(stats.fwhmMedian));
    EXPECT_TRUE(std::isfinite(stats.snrMedian));
}

TEST(FilterStatisticsEdgeCases, HandlesZeroValues)
{
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(0, 0.0, 0.0, 0.0),
        makeMetrics(0, 0.0, 0.0, 0.0),
        makeMetrics(0, 0.0, 0.0, 0.0),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("Z"), frames);

    EXPECT_TRUE(std::isfinite(stats.starsMedian));
    EXPECT_DOUBLE_EQ(stats.starsMedian, 0.0);
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 0.0);
    EXPECT_DOUBLE_EQ(stats.snrMedian, 0.0);
}

TEST(FilterStatisticsEdgeCases, HandlesLargeNumberOfFrames)
{
    std::vector<FrameQualityMetrics> frames;
    frames.reserve(10000);

    for (int i = 0; i < 10000; ++i)
    {
        frames.push_back(makeMetrics(100 + (i % 1000), // Stars vary 100-1099
                                     2.0 + (i % 100) * 0.01, // FWHM varies 2.0-2.99
                                     30.0 + (i % 50), // SNR varies 30-79
                                     0.1 + (i % 10) * 0.01 // Eccentricity varies 0.1-0.19
                                     ));
    }

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("STRESS"), frames);

    EXPECT_EQ(stats.frameCount, 10000);
    EXPECT_TRUE(std::isfinite(stats.starsMedian));
    EXPECT_TRUE(std::isfinite(stats.starsMad));
}

TEST(FilterStatisticsEdgeCases, AllFailedFramesReturnsZeroCount)
{
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(500, 2.5, 30.0, 0.2, false),
        makeMetrics(600, 2.0, 40.0, 0.1, false),
        makeMetrics(400, 3.0, 20.0, 0.3, false),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("FAIL"), frames);

    EXPECT_EQ(stats.frameCount, 0);
}

TEST(FilterStatisticsEdgeCases, TwoFramesWithLargeVariance)
{
    // Two frames with very different values
    std::vector<FrameQualityMetrics> frames = {
        makeMetrics(100, 1.0, 10.0, 0.1),
        makeMetrics(1000, 10.0, 100.0, 0.9),
    };

    const auto stats = FilterStatisticsEngine::computeStatistics(QStringLiteral("VAR"), frames);

    EXPECT_EQ(stats.frameCount, 2);
    // Median should be average of two values
    EXPECT_DOUBLE_EQ(stats.fwhmMedian, 5.5);
    EXPECT_DOUBLE_EQ(stats.snrMedian, 55.0);
}
