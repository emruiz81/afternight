// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Auto Gradient Removal: the still image its registry descriptor admits, the dialog its builder makes - the defaults
/// and ranges of the script it follows, laid out with the theme's spacing and no style sheet - and the Background
/// Modelization menu entry that launches it through its descriptor.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "app/ProcessRegistry.h"
#include "processes/AutoGradientRemovalProcess.h"
#include "processes/AutoGradientRemovalProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QWidget>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

// The process works on a still image only: its registry descriptor says so, and the launch guards it is read through
// refuse a sequence by name and admit a still colour image.
TEST(AutoGradientRemovalProcessContractTest, ItsDescriptorAdmitsAStillImageAndRefusesASequence)
{
    using namespace an::ui::process_registry;

    static_assert(AutoGradientRemovalProcess::requiresStillImageStatic());
    const ProcessDescriptor* descriptor = shared::registered("background_extraction.actionAuto_Gradient_Removal");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->requires_still_image);

    LaunchGuardInput input;
    input.has_active_image = true;
    input.active_image_is_sequence = true;
    input.active_image_channels = 3;
    EXPECT_EQ(evaluateSingleImageLaunchGuards(*descriptor, input).reason,
              LaunchGuardFailureReason::UnsupportedSequence);

    input.active_image_is_sequence = false;
    EXPECT_EQ(evaluateSingleImageLaunchGuards(*descriptor, input).reason, LaunchGuardFailureReason::None);
}

// The dialog opens at the defaults and ranges of the script the process follows: the scale, the smoothness, the
// downsampling, the structure protection, the simplified model and its degree, the correction and the preview's
// display, the outputs grouped together and the preview on.
TEST(AutoGradientRemovalProgrammaticUiTest, DefaultsAndRangesMatchSirilScriptContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildAutoGradientRemovalProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.scaleValue, nullptr);
    ASSERT_NE(ui.scaleSlider, nullptr);
    ASSERT_NE(ui.smoothnessValue, nullptr);
    ASSERT_NE(ui.smoothnessSlider, nullptr);
    ASSERT_NE(ui.downsampleComboBox, nullptr);
    ASSERT_NE(ui.structureProtection, nullptr);
    ASSERT_NE(ui.protectionThresholdValue, nullptr);
    ASSERT_NE(ui.protectionThresholdSlider, nullptr);
    ASSERT_NE(ui.protectionAmountValue, nullptr);
    ASSERT_NE(ui.protectionAmountSlider, nullptr);
    ASSERT_NE(ui.simplifiedModel, nullptr);
    ASSERT_NE(ui.polynomialDegreeValue, nullptr);
    ASSERT_NE(ui.polynomialDegreeSlider, nullptr);
    ASSERT_NE(ui.correctionModeComboBox, nullptr);
    ASSERT_NE(ui.previewDisplaySelector, nullptr);
    ASSERT_NE(ui.previewButton, nullptr);
    ASSERT_NE(ui.outputGradientModel, nullptr);
    ASSERT_NE(ui.outputProtectionMask, nullptr);
    ASSERT_NE(ui.previewCheckbox, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Auto Gradient Removal"));

    EXPECT_DOUBLE_EQ(ui.scaleValue->minimum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.scaleValue->maximum(), 10.0);
    EXPECT_DOUBLE_EQ(ui.scaleValue->singleStep(), 0.5);
    EXPECT_DOUBLE_EQ(ui.scaleValue->value(), 5.0);
    EXPECT_FALSE(ui.scaleSlider->hasTracking());

    EXPECT_DOUBLE_EQ(ui.smoothnessValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.smoothnessValue->maximum(), 3.0);
    EXPECT_DOUBLE_EQ(ui.smoothnessValue->singleStep(), 0.1);
    EXPECT_DOUBLE_EQ(ui.smoothnessValue->value(), 1.0);
    EXPECT_FALSE(ui.smoothnessSlider->hasTracking());

    EXPECT_TRUE(ui.structureProtection->isChecked());
    EXPECT_DOUBLE_EQ(ui.protectionThresholdValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.protectionThresholdValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.protectionThresholdValue->singleStep(), 0.005);
    EXPECT_DOUBLE_EQ(ui.protectionThresholdValue->value(), 0.05);
    EXPECT_FALSE(ui.protectionThresholdSlider->hasTracking());

    EXPECT_DOUBLE_EQ(ui.protectionAmountValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.protectionAmountValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.protectionAmountValue->singleStep(), 0.05);
    EXPECT_DOUBLE_EQ(ui.protectionAmountValue->value(), 0.5);
    EXPECT_FALSE(ui.protectionAmountSlider->hasTracking());

    EXPECT_FALSE(ui.simplifiedModel->isChecked());
    EXPECT_EQ(ui.polynomialDegreeValue->minimum(), 1);
    EXPECT_EQ(ui.polynomialDegreeValue->maximum(), 6);
    EXPECT_EQ(ui.polynomialDegreeValue->value(), 2);
    EXPECT_FALSE(ui.polynomialDegreeSlider->hasTracking());

    EXPECT_EQ(ui.downsampleComboBox->count(), 4);
    EXPECT_EQ(ui.downsampleComboBox->itemText(0), QStringLiteral("1"));
    EXPECT_EQ(ui.downsampleComboBox->itemText(1), QStringLiteral("2"));
    EXPECT_EQ(ui.downsampleComboBox->itemText(2), QStringLiteral("4"));
    EXPECT_EQ(ui.downsampleComboBox->itemText(3), QStringLiteral("8"));
    EXPECT_EQ(ui.downsampleComboBox->currentText(), QStringLiteral("4"));

    EXPECT_EQ(ui.correctionModeComboBox->itemText(0), QStringLiteral("Subtraction"));
    EXPECT_EQ(ui.correctionModeComboBox->itemText(1), QStringLiteral("Division"));
    EXPECT_EQ(ui.previewDisplaySelector->itemText(0), QStringLiteral("Corrected image"));
    EXPECT_EQ(ui.previewDisplaySelector->itemText(1), QStringLiteral("Gradient model"));
    EXPECT_EQ(ui.previewDisplaySelector->itemText(2), QStringLiteral("Protection mask"));
    EXPECT_EQ(ui.previewDisplaySelector->currentIndex(), 0);

    EXPECT_FALSE(ui.outputGradientModel->isChecked());
    EXPECT_FALSE(ui.outputProtectionMask->isChecked());
    ASSERT_NE(ui.outputGradientModel->parentWidget(), nullptr);
    EXPECT_EQ(ui.outputGradientModel->parentWidget()->objectName(), QStringLiteral("outputsGroup"));
    EXPECT_EQ(ui.outputProtectionMask->parentWidget(), ui.outputGradientModel->parentWidget());
    EXPECT_TRUE(ui.previewCheckbox->isChecked());
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
}

// The dialog is laid out with the theme's spacing and painted by the application's style: its margins and spacing are
// the tokens', no widget carries a style sheet, and the controls pane is given at least the width its controls ask
// for once the window is shown.
TEST(AutoGradientRemovalThemeContractTest, TheDialogIsLaidOutWithTheThemesSpacingAndNoStyleSheet)
{
    const auto& t = an::ui::tokens();
    QWidget host;
    auto ui = an::ui::processes::buildAutoGradientRemovalProcessUi(&host);
    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins(),
              QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    expectNoStyleSheet(host);

    auto* splitter = host.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    auto* controls = host.findChild<QWidget*>(QStringLiteral("controlsPanel"));
    ASSERT_NE(splitter, nullptr);
    ASSERT_NE(controls, nullptr);
    host.show();
    QApplication::processEvents();
    const QList<int> sizes = splitter->sizes();
    ASSERT_GE(sizes.size(), 2);
    const int available =
        (std::max)(0, splitter->width() - splitter->handleWidth() - splitter->widget(1)->minimumWidth());
    EXPECT_GE(sizes.front(), (std::min)(controls->sizeHint().width(), available));
}

// The Process menu's Background Modelization menu offers the process, and its entry asks the launcher for the
// process's own registry descriptor.
TEST(AutoGradientRemovalRegistrationTest, TheBackgroundModelizationMenuLaunchesItThroughItsDescriptor)
{
    ProcessMenus menus;
    QAction* entry = menus.entry(QStringLiteral("Background Modelization"), QStringLiteral("Auto Gradient Removal"));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry, menus.action("actionAuto_Gradient_Removal"));
    ASSERT_TRUE(entry->isEnabled());
    entry->trigger();
    EXPECT_EQ(menus.launched, std::vector<std::string>{"background_extraction.actionAuto_Gradient_Removal"});
}

} // namespace
