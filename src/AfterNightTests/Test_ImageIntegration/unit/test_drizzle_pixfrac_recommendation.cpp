// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// computeOptimalPixfrac unit tests
// ---------------------------------------------------------------------------

class DrizzlePixfracTest : public ::testing::Test
{
protected:
    DrizzleAnalysis makeAnalysis(int framesWithTransforms, double coverage, double medianFwhm = 0.0,
                                 int totalFrames = -1)
    {
        DrizzleAnalysis a;
        a.available = true;
        a.framesWithTransforms = framesWithTransforms;
        a.totalFrames = (totalFrames >= 0) ? totalFrames : framesWithTransforms;
        a.subPixelCoverage = coverage;
        a.meanFractionalOffset = 0.3;
        a.imageMegapixels = 24;
        a.medianFwhmPx = medianFwhm;
        return a;
    }
};

// --- Degenerate / edge cases ---

TEST_F(DrizzlePixfracTest, UnavailableAnalysis_Returns1)
{
    DrizzleAnalysis a{};
    a.available = false;
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 1.0);
}

TEST_F(DrizzlePixfracTest, TooFewFrames_Returns1)
{
    auto a = makeAnalysis(1, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 1.0);
}

TEST_F(DrizzlePixfracTest, ScaleBelowMinimum_Returns1)
{
    auto a = makeAnalysis(20, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 1.0), 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 0.5), 1.0);
}

// --- Coverage-driven behavior ---

TEST_F(DrizzlePixfracTest, ZeroCoverage_Returns1)
{
    // No sub-pixel coverage → pixfrac = 1.0 (drizzle is just upsampling)
    auto a = makeAnalysis(20, 0.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 1.0);
}

TEST_F(DrizzlePixfracTest, PerfectCoverage_20Frames_Scale2x)
{
    // Perfect coverage (1.0), 20 frames, 2× scale
    // idealPixfrac = 0.5, pixfrac = 1.0 - 1.0 * 0.5 = 0.5
    // No frame penalty (20 >= 8), no FWHM constraint
    // Rounded to 1 decimal: 0.5
    auto a = makeAnalysis(20, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.5);
}

TEST_F(DrizzlePixfracTest, HalfCoverage_20Frames_Scale2x)
{
    // 50% coverage, 20 frames, 2× scale
    // idealPixfrac = 0.5, pixfrac = 1.0 - 0.5 * 0.5 = 0.75 → rounds to 0.8
    auto a = makeAnalysis(20, 0.5);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.8);
}

TEST_F(DrizzlePixfracTest, QuarterCoverage_20Frames_Scale2x)
{
    // 25% coverage, 20 frames, 2× scale
    // pixfrac = 1.0 - 0.25 * 0.5 = 0.875 → rounds to 0.9
    auto a = makeAnalysis(20, 0.25);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.9);
}

TEST_F(DrizzlePixfracTest, PerfectCoverage_Scale3x)
{
    // Perfect coverage, 3× scale
    // idealPixfrac = 1/3 ≈ 0.333, pixfrac = 1.0 - 1.0*(1.0 - 0.333) = 0.333
    // Rounded to 1 decimal: 0.3
    auto a = makeAnalysis(20, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 3.0), 0.3);
}

// --- Frame-count penalty ---

TEST_F(DrizzlePixfracTest, FewFrames_IncreasesPixfrac)
{
    // 4 frames, perfect coverage, 2× scale
    // Base pixfrac = 0.5
    // framePenalty = 1.0 - 4/8 = 0.5, adds 0.5 * 0.15 = 0.075
    // pixfrac = 0.575, rounded to 0.6
    auto a = makeAnalysis(4, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.6);
}

TEST_F(DrizzlePixfracTest, TwoFrames_MaxFramePenalty)
{
    // 2 frames, perfect coverage, 2× scale
    // Base pixfrac = 0.5
    // framePenalty = 1.0 - 2/8 = 0.75, adds 0.75 * 0.15 = 0.1125
    // pixfrac = 0.6125, rounded to 0.6
    auto a = makeAnalysis(2, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.6);
}

TEST_F(DrizzlePixfracTest, EightFrames_NoFramePenalty)
{
    // Exactly 8 frames — at the threshold, no penalty
    auto a = makeAnalysis(8, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0), 0.5);
}

// --- FWHM (seeing) constraint ---

TEST_F(DrizzlePixfracTest, LargeFwhm_ClampsPixfrac)
{
    // Perfect coverage, lots of frames, 2× scale
    // Base pixfrac = 0.5
    // FWHM = 4.0 px → seeingLimit = (4.0 * 0.5) / 2.0 = 1.0
    // pixfrac = max(0.5, 1.0) = 1.0
    auto a = makeAnalysis(20, 1.0, 4.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0, 4.0), 1.0);
}

TEST_F(DrizzlePixfracTest, ModerateFwhm_NoEffect)
{
    // FWHM = 1.5 px → seeingLimit = (1.5 * 0.5) / 2.0 = 0.375
    // Base pixfrac = 0.5, seeingLimit < base → no effect
    // Rounded: 0.5
    auto a = makeAnalysis(20, 1.0, 1.5);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0, 1.5), 0.5);
}

TEST_F(DrizzlePixfracTest, Fwhm_RaisesAboveCoverageBaseline)
{
    // Good coverage (75%) but moderate seeing
    // Base pixfrac = 1.0 - 0.75 * 0.5 = 0.625 → 0.6
    // FWHM = 3.0 → seeingLimit = (3.0 * 0.5) / 2.0 = 0.75
    // pixfrac = max(0.625, 0.75) = 0.75 → 0.8 (rounded)
    auto a = makeAnalysis(20, 0.75, 3.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0, 3.0), 0.8);
}

TEST_F(DrizzlePixfracTest, ZeroFwhm_Ignored)
{
    // FWHM = 0 means unknown; should not affect the result
    auto a = makeAnalysis(20, 1.0);
    EXPECT_DOUBLE_EQ(computeOptimalPixfrac(a, 2.0, 0.0), 0.5);
}

// --- Clamping ---

TEST_F(DrizzlePixfracTest, ResultNeverBelow01)
{
    // Even with extreme coverage at high scale, clamp to 0.1
    auto a = makeAnalysis(100, 1.0);
    const double result = computeOptimalPixfrac(a, 20.0);
    EXPECT_GE(result, 0.1);
}

TEST_F(DrizzlePixfracTest, ResultNeverAbove10)
{
    // Even with huge FWHM, clamp to 1.0
    auto a = makeAnalysis(20, 1.0, 100.0);
    const double result = computeOptimalPixfrac(a, 2.0, 100.0);
    EXPECT_LE(result, 1.0);
}

// ---------------------------------------------------------------------------
// StackingRecommendation integration: pixfrac is set when drizzle is viable
// ---------------------------------------------------------------------------

class DrizzleRecommendationPixfracTest : public ::testing::Test
{
protected:
    DrizzleAnalysis makeViableDrizzle(double coverage, int frames = 20, double fwhm = 0.0)
    {
        DrizzleAnalysis a;
        a.available = true;
        a.framesWithTransforms = frames;
        a.totalFrames = frames;
        a.subPixelCoverage = coverage;
        a.meanFractionalOffset = 0.35;
        a.imageMegapixels = 24;
        a.medianFwhmPx = fwhm;
        return a;
    }
};

TEST_F(DrizzleRecommendationPixfracTest, Recommended_SetsPixfrac)
{
    // Excellent coverage → Recommended verdict; pixfrac should be computed
    auto drizzle = makeViableDrizzle(0.75);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);

    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::Recommended);
    EXPECT_GT(rec.drizzlePixfrac, 0.0);
    EXPECT_LE(rec.drizzlePixfrac, 1.0);
    EXPECT_LT(rec.drizzlePixfrac, 1.0); // Should be less than 1.0 with good coverage
}

TEST_F(DrizzleRecommendationPixfracTest, Possible_SetsPixfrac)
{
    // Moderate coverage → Possible verdict; pixfrac should be computed
    auto drizzle = makeViableDrizzle(0.30);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);

    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::Possible);
    EXPECT_GT(rec.drizzlePixfrac, 0.0);
    EXPECT_LE(rec.drizzlePixfrac, 1.0);
}

TEST_F(DrizzleRecommendationPixfracTest, NotRecommended_DefaultPixfrac)
{
    // Poor coverage → NotRecommended; pixfrac should stay at default
    auto drizzle = makeViableDrizzle(0.05);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);

    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::NotRecommended);
    EXPECT_DOUBLE_EQ(rec.drizzlePixfrac, 0.9); // Stays at default
}

TEST_F(DrizzleRecommendationPixfracTest, NotApplicable_ForNonLightFrames)
{
    auto drizzle = makeViableDrizzle(1.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::BIAS, 20, drizzle);

    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::NotApplicable);
    EXPECT_DOUBLE_EQ(rec.drizzlePixfrac, 0.9); // Default unchanged
}

TEST_F(DrizzleRecommendationPixfracTest, Fwhm_AffectsRecommendedPixfrac)
{
    // Same coverage, but with a large FWHM the pixfrac should be larger
    auto drizzleNoFwhm = makeViableDrizzle(0.80);
    auto drizzleWithFwhm = makeViableDrizzle(0.80, 20, 3.5);

    auto recNoFwhm =
        StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzleNoFwhm);
    auto recWithFwhm =
        StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzleWithFwhm);

    EXPECT_GE(recWithFwhm.drizzlePixfrac, recNoFwhm.drizzlePixfrac);
}

TEST_F(DrizzleRecommendationPixfracTest, MoreFrames_AllowsSmallerPixfrac)
{
    // With more frames (better coverage redundancy), pixfrac can be smaller
    auto drizzleFew = makeViableDrizzle(1.0, 4);
    auto drizzleMany = makeViableDrizzle(1.0, 40);

    auto recFew =
        StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 4, drizzleFew);
    auto recMany =
        StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 40, drizzleMany);

    // Note: 4 frames may get NotRecommended due to kDrizzleMinFrames (10).
    // Use the raw computeOptimalPixfrac instead for a clean comparison.
    const double pixfracFew = computeOptimalPixfrac(drizzleFew, 2.0);
    const double pixfracMany = computeOptimalPixfrac(drizzleMany, 2.0);

    EXPECT_LE(pixfracMany, pixfracFew);
}
