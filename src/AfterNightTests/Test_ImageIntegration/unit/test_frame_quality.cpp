// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <vector>
#include <cmath>

#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/FrameId.h"

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
// Layout guard
// ============================================================================

// FrameQualityMetrics travels through the registration UI by value, so objects that disagree
// about its size read every field after the first at the wrong offset. FrameQuality.h pins the
// layout in a tag that MSVC's linker refuses to mix (LNK2038); these expectations pin the two
// numbers that tag is built from, so a struct change that skips updating them is a test failure
// as well as a compile error, on every platform.
// See docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md.

TEST(FrameQualityMetricsLayout, MatchesThePinnedAbiTag)
{
    EXPECT_EQ(sizeof(FrameQualityMetrics), static_cast<std::size_t>(AN_FRAME_QUALITY_METRICS_SIZE));
    EXPECT_EQ(alignof(FrameQualityMetrics), static_cast<std::size_t>(AN_FRAME_QUALITY_METRICS_ALIGN));
}

TEST(FrameQualityMetricsLayout, FieldsSitWhereTheTaggedLayoutSaysTheyDo)
{
    // Enough of the layout to catch a reorder or an inserted field, which change offsets
    // without necessarily changing the size the tag records.
    EXPECT_EQ(offsetof(FrameQualityMetrics, stars), 0u);
    EXPECT_EQ(offsetof(FrameQualityMetrics, fwhm), 8u);
    EXPECT_EQ(offsetof(FrameQualityMetrics, catalog_matched_stars), 72u);
    EXPECT_EQ(offsetof(FrameQualityMetrics, succeeded), 76u);
}

// ============================================================================
// frameQualityScore() Tests
// ============================================================================

TEST(FrameQualityScore, ReturnsNegativeInfinityForFailedFrames)
{
    FrameQualityMetrics metrics = makeMetrics(1000, 2.0, 50.0, 0.1, false);
    const double score = frameQualityScore(metrics);
    EXPECT_EQ(score, -std::numeric_limits<double>::infinity());
}

TEST(FrameQualityScore, MoreStarsGivesHigherScore)
{
    FrameQualityMetrics low_stars = makeMetrics(100, 2.5, 30.0, 0.2);
    FrameQualityMetrics high_stars = makeMetrics(1000, 2.5, 30.0, 0.2);

    const double score_low = frameQualityScore(low_stars);
    const double score_high = frameQualityScore(high_stars);

    EXPECT_GT(score_high, score_low);
}

TEST(FrameQualityScore, LowerFwhmGivesHigherScore)
{
    FrameQualityMetrics good_fwhm = makeMetrics(500, 1.5, 30.0, 0.2);
    FrameQualityMetrics bad_fwhm = makeMetrics(500, 4.0, 30.0, 0.2);

    const double score_good = frameQualityScore(good_fwhm);
    const double score_bad = frameQualityScore(bad_fwhm);

    EXPECT_GT(score_good, score_bad);
}

TEST(FrameQualityScore, HigherSnrGivesHigherScore)
{
    FrameQualityMetrics low_snr = makeMetrics(500, 2.5, 10.0, 0.2);
    FrameQualityMetrics high_snr = makeMetrics(500, 2.5, 100.0, 0.2);

    const double score_low = frameQualityScore(low_snr);
    const double score_high = frameQualityScore(high_snr);

    EXPECT_GT(score_high, score_low);
}

TEST(FrameQualityScore, LowerEccentricityGivesHigherScore)
{
    FrameQualityMetrics round = makeMetrics(500, 2.5, 30.0, 0.1);
    FrameQualityMetrics elongated = makeMetrics(500, 2.5, 30.0, 0.8);

    const double score_round = frameQualityScore(round);
    const double score_elongated = frameQualityScore(elongated);

    EXPECT_GT(score_round, score_elongated);
}

TEST(FrameQualityScore, HandlesZeroStars)
{
    FrameQualityMetrics zero_stars = makeMetrics(0, 2.5, 30.0, 0.2);
    const double score = frameQualityScore(zero_stars);

    // Should still produce a finite score (log1p(0) = 0)
    EXPECT_TRUE(std::isfinite(score));
}

TEST(FrameQualityScore, HandlesZeroSnr)
{
    FrameQualityMetrics zero_snr = makeMetrics(500, 2.5, 0.0, 0.2);
    const double score = frameQualityScore(zero_snr);

    EXPECT_TRUE(std::isfinite(score));
}

TEST(FrameQualityScore, EccentricityClampedTo01)
{
    // Eccentricity values outside [0,1] should be clamped
    FrameQualityMetrics negative_ecc = makeMetrics(500, 2.5, 30.0, -0.5);
    FrameQualityMetrics over_one_ecc = makeMetrics(500, 2.5, 30.0, 1.5);
    FrameQualityMetrics zero_ecc = makeMetrics(500, 2.5, 30.0, 0.0);
    FrameQualityMetrics one_ecc = makeMetrics(500, 2.5, 30.0, 1.0);

    const double score_neg = frameQualityScore(negative_ecc);
    const double score_over = frameQualityScore(over_one_ecc);
    const double score_zero = frameQualityScore(zero_ecc);
    const double score_one = frameQualityScore(one_ecc);

    // Clamping means -0.5 behaves like 0.0, and 1.5 behaves like 1.0
    EXPECT_DOUBLE_EQ(score_neg, score_zero);
    EXPECT_DOUBLE_EQ(score_over, score_one);
}

TEST(FrameQualityScore, CombinedScoringPrefersBetterOverall)
{
    // Frame with excellent stars but poor FWHM
    FrameQualityMetrics stars_focused = makeMetrics(2000, 5.0, 20.0, 0.3);
    // Frame with moderate stars but excellent FWHM
    FrameQualityMetrics fwhm_focused = makeMetrics(500, 1.5, 40.0, 0.1);

    // Both should have reasonable scores
    const double score_stars = frameQualityScore(stars_focused);
    const double score_fwhm = frameQualityScore(fwhm_focused);

    EXPECT_TRUE(std::isfinite(score_stars));
    EXPECT_TRUE(std::isfinite(score_fwhm));
    // The scoring formula should balance multiple factors
}

TEST(FrameQualityScore, HigherZeroPointImprovesScore)
{
    FrameQualityMetrics low = makeMetrics(500, 2.5, 30.0, 0.2);
    FrameQualityMetrics high = low;
    low.photometric_zero_point = 22.0;
    high.photometric_zero_point = 24.0;

    EXPECT_GT(frameQualityScore(high), frameQualityScore(low));
}

TEST(FrameQualityScore, OneMagnitudeHigherZeroPointAddsFourPoints)
{
    FrameQualityMetrics dimmer = makeMetrics(500, 2.5, 30.0, 0.2);
    FrameQualityMetrics clearer = dimmer;
    dimmer.photometric_zero_point = 23.0;
    clearer.photometric_zero_point = 24.0;

    EXPECT_NEAR(frameQualityScore(clearer) - frameQualityScore(dimmer), 4.0, 1e-9);
}

TEST(FrameQualityScore, HigherZeroPointScatterPenalizesByTwentyPerUnit)
{
    FrameQualityMetrics low_scatter = makeMetrics(500, 2.5, 30.0, 0.2);
    FrameQualityMetrics high_scatter = low_scatter;
    low_scatter.zero_point_scatter = 0.05;
    high_scatter.zero_point_scatter = 0.30;

    EXPECT_NEAR(frameQualityScore(low_scatter) - frameQualityScore(high_scatter), 5.0, 1e-9);
}

TEST(FrameQualityScore, HigherAstrometricRmsPenalizesScore)
{
    FrameQualityMetrics good = makeMetrics(500, 2.5, 30.0, 0.2);
    FrameQualityMetrics bad = good;
    good.astrometric_rms_arcsec = 0.4;
    bad.astrometric_rms_arcsec = 1.8;

    EXPECT_GT(frameQualityScore(good), frameQualityScore(bad));
}

TEST(FrameQualityScore, HigherAstrometricRmsPenalizesByEightPerArcsecond)
{
    FrameQualityMetrics tight = makeMetrics(500, 2.5, 30.0, 0.2);
    FrameQualityMetrics loose = tight;
    tight.astrometric_rms_arcsec = 0.3;
    loose.astrometric_rms_arcsec = 1.5;

    EXPECT_NEAR(frameQualityScore(tight) - frameQualityScore(loose), 9.6, 1e-9);
}

TEST(FrameQualityScore, ZeroUnsetExtendedMetricsMatchTheBaseFormula)
{
    const FrameQualityMetrics metrics = makeMetrics(500, 2.5, 30.0, 0.2);
    const double expected = std::log1p(500.0) * 10.0 + std::log1p(30.0) * 2.0 - 2.5 * 5.0 - 0.2 * 10.0;

    EXPECT_DOUBLE_EQ(frameQualityScore(metrics), expected);
}

// ============================================================================
// selectBestReferenceFrame() Tests
// ============================================================================

TEST(SelectBestReferenceFrame, ReturnsNulloptForEmptyCandidates)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> empty;
    const auto result = selectBestReferenceFrame(empty);

    EXPECT_FALSE(result.has_value());
}

TEST(SelectBestReferenceFrame, ReturnsSingleValidFrame)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        {FrameId(QStringLiteral("C:/frame1.fit")), makeMetrics(500, 2.5, 30.0, 0.2)}};

    const auto result = selectBestReferenceFrame(candidates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/frame1.fit"));
}

TEST(SelectBestReferenceFrame, ReturnsNulloptWhenOnlyFailedFrames)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        {FrameId(QStringLiteral("C:/frame1.fit")), makeMetrics(500, 2.5, 30.0, 0.2, false)},
        {FrameId(QStringLiteral("C:/frame2.fit")), makeMetrics(1000, 2.0, 50.0, 0.1, false)}};

    const auto result = selectBestReferenceFrame(candidates);

    EXPECT_FALSE(result.has_value());
}

TEST(SelectBestReferenceFrame, SelectsFrameWithHighestScore)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        {FrameId(QStringLiteral("C:/poor.fit")), makeMetrics(100, 4.0, 10.0, 0.5)},
        {FrameId(QStringLiteral("C:/best.fit")), makeMetrics(1000, 1.5, 80.0, 0.1)},
        {FrameId(QStringLiteral("C:/medium.fit")), makeMetrics(500, 2.5, 30.0, 0.2)}};

    const auto result = selectBestReferenceFrame(candidates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/best.fit"));
}

TEST(SelectBestReferenceFrame, UsesTransparencyAndCatalogMetricsWhenAvailable)
{
    FrameQualityMetrics base_best = makeMetrics(1200, 2.2, 45.0, 0.2);
    FrameQualityMetrics enhanced_best = makeMetrics(1100, 2.3, 42.0, 0.2);
    enhanced_best.photometric_zero_point = 24.0;
    enhanced_best.catalog_matched_stars = 120;

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        {FrameId(QStringLiteral("C:/base_best.fit")), base_best},
        {FrameId(QStringLiteral("C:/enhanced_best.fit")), enhanced_best}};

    const auto result = selectBestReferenceFrame(candidates);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/enhanced_best.fit"));
}

TEST(SelectBestReferenceFrame, IgnoresFailedFramesEvenWithHighMetrics)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        // This would score highest if succeeded
        {FrameId(QStringLiteral("C:/failed.fit")), makeMetrics(5000, 1.0, 200.0, 0.0, false)},
        // This should be selected because it's the only successful one
        {FrameId(QStringLiteral("C:/succeeded.fit")), makeMetrics(100, 3.0, 10.0, 0.4)}};

    const auto result = selectBestReferenceFrame(candidates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/succeeded.fit"));
}

TEST(SelectBestReferenceFrame, DeterministicTieBreaker)
{
    // Two frames with identical metrics - should use FrameId comparison as tie-breaker
    FrameQualityMetrics same_metrics = makeMetrics(500, 2.5, 30.0, 0.2);

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates = {
        {FrameId(QStringLiteral("C:/z_frame.fit")), same_metrics},
        {FrameId(QStringLiteral("C:/a_frame.fit")), same_metrics}};

    const auto result1 = selectBestReferenceFrame(candidates);

    // Reverse the order and verify same result
    std::vector<std::pair<FrameId, FrameQualityMetrics>> reversed = {
        {FrameId(QStringLiteral("C:/a_frame.fit")), same_metrics},
        {FrameId(QStringLiteral("C:/z_frame.fit")), same_metrics}};

    const auto result2 = selectBestReferenceFrame(reversed);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    // Both should return the same frame (deterministic)
    EXPECT_EQ(result1->canonicalPath(), result2->canonicalPath());
}

TEST(SelectBestReferenceFrame, HandlesLargeNumberOfCandidates)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.reserve(1000);

    for (int i = 0; i < 1000; ++i)
    {
        QString path = QStringLiteral("C:/frame_%1.fit").arg(i, 4, 10, QLatin1Char('0'));
        // Make frame 500 the best
        int stars = (i == 500) ? 5000 : 100 + i;
        candidates.emplace_back(FrameId(path), makeMetrics(stars, 2.5, 30.0, 0.2));
    }

    const auto result = selectBestReferenceFrame(candidates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/frame_0500.fit"));
}

TEST(SelectBestReferenceFrame, PrefersSuceededOverFailed)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;

    // Add many failed frames with excellent metrics
    for (int i = 0; i < 10; ++i)
    {
        QString path = QStringLiteral("C:/failed_%1.fit").arg(i);
        candidates.emplace_back(FrameId(path), makeMetrics(10000, 0.5, 500.0, 0.0, false));
    }

    // Add one mediocre but successful frame
    candidates.emplace_back(FrameId(QStringLiteral("C:/success.fit")), makeMetrics(50, 5.0, 5.0, 0.9, true));

    const auto result = selectBestReferenceFrame(candidates);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->canonicalPath(), QStringLiteral("C:/success.fit"));
}

// ============================================================================
// detectOutliers() Tests
// ============================================================================

TEST(DetectOutliers, ReturnsEmptyWhenAllFramesAreGood)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> frames;
    // Create 10 frames with identical good metrics
    for (int i = 0; i < 10; ++i)
    {
        frames.emplace_back(FrameId(QStringLiteral("frame_%1.fit").arg(i)), makeMetrics(1000, 2.0, 50.0, 0.1));
    }

    auto outliers = detectOutliers(frames, 3.0);
    EXPECT_TRUE(outliers.empty());
}

TEST(DetectOutliers, IdentifiesLowScoreOutlier)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> frames;
    // 20 good frames
    for (int i = 0; i < 20; ++i)
    {
        frames.emplace_back(FrameId(QStringLiteral("good_%1.fit").arg(i)), makeMetrics(1000, 2.0, 50.0, 0.1));
    }
    // 1 bad frame (very few stars, high FWHM)
    frames.emplace_back(FrameId(QStringLiteral("bad.fit")), makeMetrics(50, 6.0, 10.0, 0.6));

    auto outliers = detectOutliers(frames, 3.0);

    ASSERT_EQ(outliers.size(), 1u);
    // FrameId canonicalizes the path, so compare against canonical path of input
    EXPECT_EQ(outliers[0].canonicalPath(), FrameId(QStringLiteral("bad.fit")).canonicalPath());
}

TEST(DetectOutliers, HandlesEmptyInput)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> empty;
    auto outliers = detectOutliers(empty, 3.0);
    EXPECT_TRUE(outliers.empty());
}

TEST(DetectOutliers, IgnoresFailedFramesInCalculationButFlagsThem)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> frames;
    for (int i = 0; i < 10; ++i)
        frames.emplace_back(FrameId(QStringLiteral("good_%1.fit").arg(i)), makeMetrics(1000, 2.0, 50.0, 0.1));

    frames.emplace_back(FrameId(QStringLiteral("failed.fit")), makeMetrics(0, 0, 0, 0, false));

    auto outliers = detectOutliers(frames, 3.0);

    // Failed frame should be detected
    ASSERT_FALSE(outliers.empty());
    bool found = false;
    for (const auto& id : outliers)
        if (id.canonicalPath().contains("failed"))
            found = true;
    EXPECT_TRUE(found);
}

TEST(DetectOutliers, FlagsZeroPointOutlierWhenExtendedMetricsPresent)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> frames;
    for (int i = 0; i < 20; ++i)
    {
        auto metrics = makeMetrics(1000, 2.0, 50.0, 0.1);
        metrics.photometric_zero_point = 25.0;
        frames.emplace_back(FrameId(QStringLiteral("good_%1.fit").arg(i)), metrics);
    }

    auto bad = makeMetrics(1000, 2.0, 50.0, 0.1);
    bad.photometric_zero_point = 18.0;
    frames.emplace_back(FrameId(QStringLiteral("bad_zp.fit")), bad);

    const auto outliers = detectOutliers(frames, 3.0);
    ASSERT_EQ(outliers.size(), 1u);
    EXPECT_EQ(outliers.front().canonicalPath(), FrameId(QStringLiteral("bad_zp.fit")).canonicalPath());
}
