// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Adjust Brightness/Contrast dialog as its builder makes it: the widgets the window reads with their ranges and
/// defaults, and each spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/AdjustBrightnessAndContrastProcessUiBuilder.h"

namespace
{

// The dialog opens with brightness from -150 to 150 and contrast from -100 to 100, both at zero, the preview on, the
// histogram grid, clipping figures and histogram shown, local normalization off, and Apply, Close and Reset.
TEST(AdjustBrightnessAndContrastProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildAdjustBrightnessAndContrastProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.zoomIn, nullptr);
    ASSERT_NE(ui.zoomOut, nullptr);
    ASSERT_NE(ui.zoomReset, nullptr);
    ASSERT_NE(ui.gridButton, nullptr);
    ASSERT_NE(ui.showClippingInfo, nullptr);
    ASSERT_NE(ui.localChannelNormalization, nullptr);
    ASSERT_NE(ui.showHistogramAreas, nullptr);
    ASSERT_NE(ui.showHistogram, nullptr);
    ASSERT_NE(ui.plotResolution, nullptr);
    ASSERT_NE(ui.brightnessValue, nullptr);
    ASSERT_NE(ui.brightnessSlider, nullptr);
    ASSERT_NE(ui.contrastValue, nullptr);
    ASSERT_NE(ui.contrastSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Adjust Brightness/Contrast"));

    EXPECT_EQ(ui.brightnessValue->minimum(), -150);
    EXPECT_EQ(ui.brightnessValue->maximum(), 150);
    EXPECT_EQ(ui.brightnessValue->value(), 0);
    EXPECT_EQ(ui.brightnessSlider->minimum(), -150);
    EXPECT_EQ(ui.brightnessSlider->maximum(), 150);
    EXPECT_EQ(ui.brightnessSlider->value(), 0);

    EXPECT_EQ(ui.contrastValue->minimum(), -100);
    EXPECT_EQ(ui.contrastValue->maximum(), 100);
    EXPECT_EQ(ui.contrastValue->value(), 0);
    EXPECT_EQ(ui.contrastSlider->minimum(), -100);
    EXPECT_EQ(ui.contrastSlider->maximum(), 100);
    EXPECT_EQ(ui.contrastSlider->value(), 0);

    EXPECT_TRUE(ui.gridButton->isCheckable());
    EXPECT_TRUE(ui.gridButton->isChecked());
    EXPECT_TRUE(ui.showClippingInfo->isCheckable());
    EXPECT_TRUE(ui.showClippingInfo->isChecked());
    EXPECT_TRUE(ui.localChannelNormalization->isCheckable());
    EXPECT_FALSE(ui.localChannelNormalization->isChecked());
    EXPECT_TRUE(ui.showHistogram->isCheckable());
    EXPECT_TRUE(ui.showHistogram->isChecked());
    EXPECT_TRUE(ui.showHistogramAreas->isCheckable());
    EXPECT_TRUE(ui.preview->isChecked());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// Bound as the window binds them, each spin box and its slider move together and a change of either is reported.
TEST(AdjustBrightnessAndContrastProgrammaticUiTest, BrightnessAndContrastControlsStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildAdjustBrightnessAndContrastProcessUi(&host);

    QSliderSpinBox brightnessControl(ui.brightnessValue, ui.brightnessSlider, &host);
    QSignalSpy brightnessSpy(&brightnessControl, &QSliderSpinBox::valueChanged);

    ui.brightnessValue->setValue(42);
    QApplication::processEvents();
    EXPECT_GT(brightnessSpy.count(), 0);
    EXPECT_EQ(ui.brightnessSlider->value(), 42);

    ui.brightnessSlider->setValue(-27);
    QApplication::processEvents();
    EXPECT_EQ(ui.brightnessValue->value(), -27);

    QSliderSpinBox contrastControl(ui.contrastValue, ui.contrastSlider, &host);
    QSignalSpy contrastSpy(&contrastControl, &QSliderSpinBox::valueChanged);

    ui.contrastValue->setValue(30);
    QApplication::processEvents();
    EXPECT_GT(contrastSpy.count(), 0);
    EXPECT_EQ(ui.contrastSlider->value(), 30);

    ui.contrastSlider->setValue(-15);
    QApplication::processEvents();
    EXPECT_EQ(ui.contrastValue->value(), -15);
}

} // anonymous namespace
