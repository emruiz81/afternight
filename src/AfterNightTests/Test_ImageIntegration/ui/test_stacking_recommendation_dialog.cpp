// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTest>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QDialog>
#include <QVBoxLayout>

#include "processes/ImageIntegration/controls/StackingRecommendationDialog.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

class StackingRecommendationDialogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Mock recommendation data
        rec.stackMethod = "Average";
        rec.normalizationMode = "Additive With Scaling";
        rec.rejectionMethod = "Winsorized Sigma Clipping";
        rec.sigmaLow = 3.2;
        rec.sigmaHigh = 3.2;
        rec.weightingMethod = "Weighted FWHM";
        rec.localNormalization = true;
        rec.reason = "Mock Primary Reason";

        // Mock drizzle analysis
        drizzle.available = true;
        drizzle.subPixelCoverage = 0.8;
        drizzle.framesWithTransforms = 20;
        drizzle.totalFrames = 20;
        rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::Recommended;
        rec.drizzleScale = 2.0;
        rec.drizzleReason = "Mock Drizzle Reason";

        // Mock current settings
        currentSettings.stackMethod = "Average";
        currentSettings.normalizationMode = NormalizationMode::None;
        currentSettings.rejectionMethod = "None";
    }

    StackingRecommendation rec;
    DrizzleAnalysis drizzle;
    StackingSettings currentSettings;
};

TEST_F(StackingRecommendationDialogTest, DialogTitleIncludesGroupLabel)
{
    QString groupLabel = "My Test Group";
    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, groupLabel, 20, rec, drizzle,
                                        currentSettings);

    // Dialog title is "Recommended Stacking Settings", but the header inside contains the group label.
    // Let's check window title first.
    EXPECT_EQ(dialog.windowTitle(), "Recommended Stacking Settings");

    // Check header label for group name
    QList<QLabel*> labels = dialog.findChildren<QLabel*>();
    bool foundGroupLabel = false;
    for (QLabel* label : labels)
    {
        if (label->text().contains(groupLabel))
        {
            foundGroupLabel = true;
            break;
        }
    }
    EXPECT_TRUE(foundGroupLabel) << "Header label should contain group name";
}

TEST_F(StackingRecommendationDialogTest, DisplaysPrimaryRecommendations)
{
    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, "Group 1", 20, rec, drizzle,
                                        currentSettings);

    QList<QLabel*> labels = dialog.findChildren<QLabel*>();

    // Check if rejection method is displayed
    bool foundRejection = false;
    for (QLabel* label : labels)
    {
        if (label->text().contains(rec.rejectionMethod))
        {
            foundRejection = true;
            break;
        }
    }
    EXPECT_TRUE(foundRejection) << "Should display primary rejection method";

    // Check if weighting is displayed
    bool foundWeighting = false;
    for (QLabel* label : labels)
    {
        if (label->text().contains(rec.weightingMethod))
        {
            foundWeighting = true;
            break;
        }
    }
    EXPECT_TRUE(foundWeighting) << "Should display weighting method";
}

TEST_F(StackingRecommendationDialogTest, DisplaysDrizzleRecommendation)
{
    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, "Group 1", 20, rec, drizzle,
                                        currentSettings);

    QList<QLabel*> labels = dialog.findChildren<QLabel*>();

    // Check for drizzle reason
    bool foundReason = false;
    for (QLabel* label : labels)
    {
        if (label->text() == rec.drizzleReason)
        {
            foundReason = true;
            break;
        }
    }
    EXPECT_TRUE(foundReason) << "Should display drizzle reason";

    // Check for "RECOMMENDED" badge
    bool foundBadge = false;
    for (QLabel* label : labels)
    {
        if (label->text() == "RECOMMENDED")
        {
            foundBadge = true;
            break;
        }
    }
    EXPECT_TRUE(foundBadge) << "Should display drizzle verdict badge";

    // Check for coverage widget
    auto* coverageWidget = dialog.findChild<DrizzleCoverageWidget*>();
    EXPECT_TRUE(coverageWidget != nullptr) << "Should contain DrizzleCoverageWidget";
}

TEST_F(StackingRecommendationDialogTest, ShowsAlternativeOptionWhenAvailable)
{
    // Setup alternative
    rec.secondary.available = true;
    rec.secondary.label = "Satellite Trail Removal";
    rec.secondary.rejectionMethod = "Linear Fit";
    rec.secondary.reason = "Mock Alt Reason";

    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, "Group 1", 20, rec, drizzle,
                                        currentSettings);

    // Look for alternative frame
    QFrame* altFrame = dialog.findChild<QFrame*>("altFrame");
    ASSERT_TRUE(altFrame != nullptr) << "Should have alternative option frame";

    // Check header
    QList<QLabel*> labels = altFrame->findChildren<QLabel*>();
    bool foundHeader = false;
    for (QLabel* label : labels)
    {
        if (label->text().contains("SATELLITE TRAIL REMOVAL"))
        {
            foundHeader = true;
            break;
        }
    }
    EXPECT_TRUE(foundHeader) << "Should display alternative label";
}

TEST_F(StackingRecommendationDialogTest, AppliesPrimaryRecommendationByDefault)
{
    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, "Group 1", 20, rec, drizzle,
                                        currentSettings);

    // Simulate clicking OK/Apply (default button)
    // We can't easily click standard dialog buttons without showing,
    // but we can check initial getRecommendation() state if logic was different.
    // Actually, getRecommendation() returns m_recommendation which is initialized with `rec`.

    StackingRecommendation result = dialog.getRecommendation();
    EXPECT_EQ(result.rejectionMethod, "Winsorized Sigma Clipping");
    EXPECT_EQ(result.localNormalization, true);
}

TEST_F(StackingRecommendationDialogTest, AppliesAlternativeRecommendation)
{
    rec.secondary.available = true;
    rec.secondary.rejectionMethod = "Linear Fit";
    rec.secondary.sigmaLow = 5.0;
    rec.secondary.localNormalization = false;

    StackingRecommendationDialog dialog(StackingSettingsDialog::FrameType::LIGHT, "Group 1", 20, rec, drizzle,
                                        currentSettings);

    // Find "Apply Alternative" button inside altFrame
    QFrame* altFrame = dialog.findChild<QFrame*>("altFrame");
    ASSERT_TRUE(altFrame != nullptr);

    QPushButton* altBtn = altFrame->findChild<QPushButton*>();
    ASSERT_TRUE(altBtn != nullptr);
    EXPECT_TRUE(altBtn->text().contains("Apply Alternative"));

    // Click it
    altBtn->click();

    // Dialog should be accepted and apply requested
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(dialog.isApplyRequested());

    // Recommendation should be updated to secondary
    StackingRecommendation result = dialog.getRecommendation();
    EXPECT_EQ(result.rejectionMethod, "Linear Fit");
    EXPECT_EQ(result.sigmaLow, 5.0);
    EXPECT_EQ(result.localNormalization, false);
}
