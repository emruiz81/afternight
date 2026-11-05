// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QObject>
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"

#include <algorithm>
#include <cmath>

namespace
{
// Helper to create drizzle analysis data
DrizzleAnalysis createDrizzleAnalysis(bool available, int total, int withTransforms, double coverage, double mp)
{
    DrizzleAnalysis d;
    d.available = available;
    d.totalFrames = total;
    d.framesWithTransforms = withTransforms;
    d.subPixelCoverage = coverage;
    d.imageMegapixels = static_cast<int>(mp);
    return d;
}
} // namespace

class StackingRecommendationLogicTest : public ::testing::Test
{
protected:
    // Constants from implementation for assertions
    const double kSigmaConservative = 3.0;
    const double kSigmaTight = 2.5;

    // Helper to calculate expected dynamic sigma
    double expectedDynamicSigma(int count, double base = 3.5)
    {
        if (count < 10)
            return 3.0;
        double sigma = base - 0.5 * std::log10(static_cast<double>(count));
        return std::clamp(std::round(sigma * 10.0) / 10.0, 2.0, 3.5);
    }
};

// --- BIAS FRAMES ---

TEST_F(StackingRecommendationLogicTest, Bias_LowFrameCount_NoRejection)
{
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::BIAS, 5);
    EXPECT_EQ(rec.rejectionMethod, "None");
    EXPECT_EQ(rec.normalizationMode, "None");
}

TEST_F(StackingRecommendationLogicTest, Bias_MediumFrameCount_SigmaClipping)
{
    int frames = 15;
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::BIAS, frames);
    EXPECT_EQ(rec.rejectionMethod, "Sigma Clipping");
    EXPECT_EQ(rec.sigmaLow, expectedDynamicSigma(frames));
}

TEST_F(StackingRecommendationLogicTest, Bias_HighFrameCount_Winsorized)
{
    int frames = 25;
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::BIAS, frames);
    EXPECT_EQ(rec.rejectionMethod, "Winsorized Sigma Clipping");
    EXPECT_EQ(rec.sigmaLow, expectedDynamicSigma(frames));
}

// --- DARK FRAMES ---

TEST_F(StackingRecommendationLogicTest, Dark_FollowsBiasLogic)
{
    // Dark logic is identical to Bias logic structure-wise
    auto recLow = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::DARK, 5);
    EXPECT_EQ(recLow.rejectionMethod, "None");

    auto recHigh = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::DARK, 30);
    EXPECT_EQ(recHigh.rejectionMethod, "Winsorized Sigma Clipping");
}

// --- FLAT FRAMES ---

TEST_F(StackingRecommendationLogicTest, Flat_NormalizationIsMultiplicative)
{
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::FLAT, 20);
    EXPECT_EQ(rec.normalizationMode, "Multiplicative");
}

TEST_F(StackingRecommendationLogicTest, Flat_Rejection_SigmaClipping)
{
    // Flats prefer Sigma Clipping to preserve vignetting
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::FLAT, 30);
    EXPECT_EQ(rec.rejectionMethod, "Sigma Clipping");
}

TEST_F(StackingRecommendationLogicTest, Flat_SecondaryRecommendation_Winsorized)
{
    // For large flat stacks, Winsorized is offered as alternative
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::FLAT, 30);
    EXPECT_TRUE(rec.secondary.available);
    EXPECT_EQ(rec.secondary.rejectionMethod, "Winsorized Sigma Clipping");
}

// --- LIGHT FRAMES ---

TEST_F(StackingRecommendationLogicTest, Light_LowFrameCount_Percentile)
{
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 5);
    EXPECT_EQ(rec.rejectionMethod, "Percentile");
    EXPECT_EQ(rec.sigmaLow, 0.2); // kPercentileLow
}

TEST_F(StackingRecommendationLogicTest, Light_MediumFrameCount_SigmaClipping)
{
    int frames = 12;
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, frames);
    EXPECT_EQ(rec.rejectionMethod, "Sigma Clipping");
    // Should NOT trigger local normalization yet (<15)
    EXPECT_FALSE(rec.localNormalization);
}

TEST_F(StackingRecommendationLogicTest, Light_HighFrameCount_Winsorized_WithSecondary)
{
    int frames = 20; // 15-24 range for Winsorized
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, frames);

    // Primary
    EXPECT_EQ(rec.rejectionMethod, "Winsorized Sigma Clipping");
    EXPECT_TRUE(rec.localNormalization); // >15 frames triggers LN

    // Secondary: Linear Fit (better for satellite trails)
    EXPECT_TRUE(rec.secondary.available);
    EXPECT_EQ(rec.secondary.rejectionMethod, "Linear Fit");
    EXPECT_EQ(rec.secondary.label, "Satellite Trail Removal");
}

TEST_F(StackingRecommendationLogicTest, Light_HigherFrameCount_LinearFit_WithSecondary)
{
    int frames = 30; // 25-39 range for Linear Fit
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, frames);

    // Primary
    EXPECT_EQ(rec.rejectionMethod, "Linear Fit");

    // Secondary: Winsorized (better for SNR)
    EXPECT_TRUE(rec.secondary.available);
    EXPECT_EQ(rec.secondary.rejectionMethod, "Winsorized Sigma Clipping");
    EXPECT_EQ(rec.secondary.label, "Maximum SNR");
}

TEST_F(StackingRecommendationLogicTest, Light_VeryHighFrameCount_GESDT)
{
    int frames = 60;
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, frames);

    // Primary
    EXPECT_EQ(rec.rejectionMethod, "GESDT");
    EXPECT_TRUE(rec.localNormalization);

    // Secondary: Linear Fit still available
    EXPECT_TRUE(rec.secondary.available);
    EXPECT_EQ(rec.secondary.rejectionMethod, "Linear Fit");
}

TEST_F(StackingRecommendationLogicTest, Light_Weighting_Threshold)
{
    // < 8 frames: No weighting recommended (too unstable)
    auto recLow = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 5);
    EXPECT_TRUE(recLow.weightingMethod.isEmpty());

    // >= 8 frames: Weighted FWHM
    auto recHigh = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 10);
    EXPECT_EQ(recHigh.weightingMethod, "Weighted FWHM");
}

// --- DRIZZLE ANALYSIS ---

TEST_F(StackingRecommendationLogicTest, Drizzle_NotAvailable_IfNoData)
{
    auto drizzle = createDrizzleAnalysis(false, 20, 0, 0.0, 24.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    // Verdict should be NotApplicable if !available, OR the code might return NotApplicable/NotRecommended depending on
    // implementation details. Based on implementation: if (drizzle.available) block is entered. If not entered, verdict
    // remains default (NotApplicable).
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::NotApplicable);
}

TEST_F(StackingRecommendationLogicTest, Drizzle_NotRecommended_MissingTransforms)
{
    // 20 frames total, but only 10 have transforms
    auto drizzle = createDrizzleAnalysis(true, 20, 10, 0.8, 24.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::NotRecommended);
    EXPECT_TRUE(rec.drizzleReason.contains("Only 10 of 20 frames"));
}

TEST_F(StackingRecommendationLogicTest, Drizzle_NotRecommended_BadCoverage)
{
    // Good frame count, but poor dither (0.1 coverage)
    auto drizzle = createDrizzleAnalysis(true, 20, 20, 0.1, 24.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::NotRecommended);
    EXPECT_TRUE(rec.drizzleReason.contains("Sub-pixel coverage is very low"));
}

TEST_F(StackingRecommendationLogicTest, Drizzle_Possible_MediumCoverage)
{
    // 0.35 coverage (between 0.25 and 0.50)
    auto drizzle = createDrizzleAnalysis(true, 20, 20, 0.35, 24.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::Possible);
    EXPECT_EQ(rec.drizzleScale, 2.0);
}

TEST_F(StackingRecommendationLogicTest, Drizzle_Recommended_GoodCoverage)
{
    // 0.8 coverage (excellent)
    auto drizzle = createDrizzleAnalysis(true, 20, 20, 0.8, 24.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::Recommended);
    EXPECT_EQ(rec.drizzleScale, 2.0);
}

TEST_F(StackingRecommendationLogicTest, Drizzle_LargeSensor_Warning)
{
    // 60 MP sensor -> should warn about size
    auto drizzle = createDrizzleAnalysis(true, 20, 20, 0.8, 60.0);
    auto rec = StackingSettingsDialog::getRecommendedSettings(StackingSettingsDialog::FrameType::LIGHT, 20, drizzle);
    EXPECT_EQ(rec.drizzleVerdict, StackingRecommendation::DrizzleVerdict::Recommended);
    EXPECT_TRUE(rec.drizzleReason.contains("sufficient RAM"));
}
