// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Integration tabs and settings dialogs as their builders make them - the calibration, registration,
/// stacking and summary tabs and the local normalization, registration and stacking settings dialogs: the widgets each
/// reads with their defaults, and the stacking settings growing with their dialog.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialog>
#include <QWidget>

#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/tabs/CalibrationTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/RegistrationTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/StackingTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/SummaryTabUiBuilder.h"

namespace
{

// The calibration tab opens at its size with the widgets the window reads.
TEST(ImageIntegrationTabsProgrammaticUiTest, CalibrationTabConstructorDefaultsMatchContract)
{
    QWidget host;
    an::ui::imageintegration::CalibrationTabUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("CalibrationTab"));
    EXPECT_EQ(host.size(), QSize(900, 600));
    ASSERT_NE(ui.mainSplitter, nullptr);
    ASSERT_NE(ui.scrollArea, nullptr);
    ASSERT_NE(ui.panelLayout, nullptr);
    ASSERT_NE(ui.rightPanelScrollArea, nullptr);
    ASSERT_NE(ui.rightPanelLayout, nullptr);
}

// The registration tab opens with its group settings and run out of use until there is something to register, and its
// quality thresholds folded.
TEST(ImageIntegrationTabsProgrammaticUiTest, RegistrationTabConstructorDefaultsMatchContract)
{
    QWidget host;
    an::ui::imageintegration::RegistrationTabUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("RegistrationTab"));
    ASSERT_NE(ui.grpRegistrationGroups, nullptr);
    ASSERT_NE(ui.tblRegistrationGroups, nullptr);
    ASSERT_NE(ui.grpFrameQuality, nullptr);
    ASSERT_NE(ui.tblFrameQuality, nullptr);
    ASSERT_NE(ui.btnRunRegistration, nullptr);
    EXPECT_FALSE(ui.btnGroupSettings->isEnabled());
    EXPECT_FALSE(ui.btnRunRegistration->isEnabled());
    ASSERT_NE(ui.grpQualityThresholds, nullptr);
    EXPECT_FALSE(ui.grpQualityThresholds->isExpanded());
}

// The stacking tab opens with its runs and exposure tolerance out of use until there is something to stack, and opens
// what it integrates.
TEST(ImageIntegrationTabsProgrammaticUiTest, StackingTabConstructorDefaultsMatchContract)
{
    QWidget host;
    an::ui::imageintegration::StackingTabUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("StackingTab"));
    ASSERT_NE(ui.grpIntegrationGroups, nullptr);
    ASSERT_NE(ui.tblIntegrationGroups, nullptr);
    ASSERT_NE(ui.grpExecutionOptions, nullptr);
    ASSERT_NE(ui.btnRunIntegration, nullptr);
    EXPECT_FALSE(ui.btnRunIntegration->isEnabled());
    EXPECT_FALSE(ui.btnRunIntegrationGroup->isEnabled());
    EXPECT_FALSE(ui.spinExposureTolerance->isEnabled());
    EXPECT_TRUE(ui.chkOpenIntegratedFiles->isChecked());
}

// The summary tab opens with Execute Integration.
TEST(ImageIntegrationTabsProgrammaticUiTest, SummaryTabConstructorDefaultsMatchContract)
{
    QWidget host;
    an::ui::imageintegration::SummaryTabUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("SummaryTab"));
    ASSERT_NE(ui.statusCardFrame, nullptr);
    ASSERT_NE(ui.integrationSummaryFrame, nullptr);
    ASSERT_NE(ui.sessionNotesFrame, nullptr);
    ASSERT_NE(ui.runHistoryFrame, nullptr);
    ASSERT_NE(ui.btnExecute, nullptr);
    EXPECT_EQ(ui.btnExecute->text(), QStringLiteral("Execute Integration"));
}

// The local normalization settings open with their advanced settings off and clustered sources allowed.
TEST(ImageIntegrationTabsProgrammaticUiTest, LocalNormalizationDialogConstructorDefaultsMatchContract)
{
    QDialog host;
    an::ui::imageintegration::LocalNormalizationSettingsDialogUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("LocalNormalizationSettingsDialog"));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Local Normalization Settings"));
    ASSERT_NE(ui.presetSelector, nullptr);
    ASSERT_NE(ui.referenceStrategy, nullptr);
    ASSERT_NE(ui.gridSize, nullptr);
    ASSERT_NE(ui.advancedGroupBox, nullptr);
    EXPECT_FALSE(ui.chkEnableAdvanced->isChecked());
    EXPECT_TRUE(ui.allowClusteredSources->isChecked());
}

// The registration settings open on the standard alignment with no amplitude or roundness range.
TEST(ImageIntegrationTabsProgrammaticUiTest, RegistrationSettingsDialogConstructorDefaultsMatchContract)
{
    QDialog host;
    an::ui::imageintegration::RegistrationSettingsDialogUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("RegistrationSettingsDialog"));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Registration Settings"));
    ASSERT_NE(ui.minStarPairsSpinBox, nullptr);
    ASSERT_NE(ui.maxStarPairsSpinBox, nullptr);
    ASSERT_NE(ui.referenceImageComboBox, nullptr);
    ASSERT_NE(ui.alignModeStandard, nullptr);
    ASSERT_NE(ui.advancedSettingsButton, nullptr);
    EXPECT_TRUE(ui.alignModeStandard->isChecked());
    EXPECT_FALSE(ui.chkEnableAmplitudeRange->isChecked());
    EXPECT_FALSE(ui.chkEnableRoundnessRange->isChecked());
}

// The stacking settings open with drizzle off.
TEST(ImageIntegrationTabsProgrammaticUiTest, StackingSettingsDialogConstructorDefaultsMatchContract)
{
    QDialog host;
    an::ui::imageintegration::StackingSettingsDialogUiElements ui;
    ui.setupUi(&host);

    EXPECT_EQ(host.objectName(), QStringLiteral("StackingSettingsDialog"));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Stacking Settings"));
    ASSERT_NE(ui.stackMethodSelector, nullptr);
    ASSERT_NE(ui.normalizationMode, nullptr);
    ASSERT_NE(ui.rejectionMethodSelector, nullptr);
    ASSERT_NE(ui.weightingOptionsButton, nullptr);
    ASSERT_NE(ui.drizzleSettingsGroupBox, nullptr);
    ASSERT_NE(ui.resetButton, nullptr);
    ASSERT_NE(ui.applyToAllButton, nullptr);
    ASSERT_NE(ui.chkEnableDrizzle, nullptr);
    EXPECT_FALSE(ui.chkEnableDrizzle->isChecked());
}

// The stacking settings' groups grow with their dialog, and their selectors never shrink.
TEST(ImageIntegrationTabsProgrammaticUiTest, StackingSettingsDialogRowsExpandWithDialogWidth)
{
    QDialog host;
    an::ui::imageintegration::StackingSettingsDialogUiElements ui;
    ui.setupUi(&host);

    ASSERT_NE(ui.stackSettingsGroupBox, nullptr);
    ASSERT_NE(ui.rejectionSettingsGroupBox, nullptr);
    ASSERT_NE(ui.drizzleSettingsGroupBox, nullptr);
    ASSERT_NE(ui.stackMethodSelector, nullptr);
    ASSERT_NE(ui.rejectionMethodSelector, nullptr);
    ASSERT_NE(ui.drizzleScaleSelector, nullptr);

    host.resize(500, 580);
    host.show();
    QApplication::processEvents();
    if (host.layout())
    {
        host.layout()->activate();
    }

    const int hostWidthBefore = host.width();
    const int stackGroupWidthBefore = ui.stackSettingsGroupBox->width();
    const int rejectionGroupWidthBefore = ui.rejectionSettingsGroupBox->width();
    const int drizzleGroupWidthBefore = ui.drizzleSettingsGroupBox->width();
    const int stackComboWidthBefore = ui.stackMethodSelector->width();
    const int rejectionComboWidthBefore = ui.rejectionMethodSelector->width();
    const int drizzleComboWidthBefore = ui.drizzleScaleSelector->width();

    host.resize(hostWidthBefore + 300, host.height());
    QApplication::processEvents();
    if (host.layout())
    {
        host.layout()->activate();
    }

    ASSERT_GT(host.width(), hostWidthBefore);
    EXPECT_GT(ui.stackSettingsGroupBox->width(), stackGroupWidthBefore);
    EXPECT_GT(ui.rejectionSettingsGroupBox->width(), rejectionGroupWidthBefore);
    EXPECT_GT(ui.drizzleSettingsGroupBox->width(), drizzleGroupWidthBefore);
    EXPECT_GE(ui.stackMethodSelector->width(), stackComboWidthBefore);
    EXPECT_GE(ui.rejectionMethodSelector->width(), rejectionComboWidthBefore);
    EXPECT_GE(ui.drizzleScaleSelector->width(), drizzleComboWidthBefore);
}

} // namespace
