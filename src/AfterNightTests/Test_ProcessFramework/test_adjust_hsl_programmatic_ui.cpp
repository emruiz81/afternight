// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Adjust HSL dialog as its builder makes it: the widgets the window reads with their ranges and defaults, and each
/// spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/AdjustHSLProcessUiBuilder.h"

namespace
{

// The dialog opens with hue from -180 to 180 and saturation and lightness from -100 to 100, all at zero, the preview
// on, the histogram grid, clipping figures and histogram shown, local normalization off, and Apply, Close and Reset.
TEST(AdjustHSLProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildAdjustHSLProcessUi(&host);

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
    ASSERT_NE(ui.hueValue, nullptr);
    ASSERT_NE(ui.hueSlider, nullptr);
    ASSERT_NE(ui.saturationValue, nullptr);
    ASSERT_NE(ui.saturationSlider, nullptr);
    ASSERT_NE(ui.lightnessValue, nullptr);
    ASSERT_NE(ui.lightnessSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Adjust HSL"));

    EXPECT_EQ(ui.hueValue->minimum(), -180);
    EXPECT_EQ(ui.hueValue->maximum(), 180);
    EXPECT_EQ(ui.hueValue->value(), 0);

    EXPECT_EQ(ui.hueSlider->minimum(), -180);
    EXPECT_EQ(ui.hueSlider->maximum(), 180);
    EXPECT_EQ(ui.hueSlider->orientation(), Qt::Horizontal);
    EXPECT_EQ(ui.hueSlider->value(), 0);

    EXPECT_EQ(ui.saturationValue->minimum(), -100);
    EXPECT_EQ(ui.saturationValue->maximum(), 100);
    EXPECT_EQ(ui.saturationValue->value(), 0);

    EXPECT_EQ(ui.saturationSlider->minimum(), -100);
    EXPECT_EQ(ui.saturationSlider->maximum(), 100);
    EXPECT_EQ(ui.saturationSlider->orientation(), Qt::Horizontal);
    EXPECT_EQ(ui.saturationSlider->value(), 0);

    EXPECT_EQ(ui.lightnessValue->minimum(), -100);
    EXPECT_EQ(ui.lightnessValue->maximum(), 100);
    EXPECT_EQ(ui.lightnessValue->value(), 0);

    EXPECT_EQ(ui.lightnessSlider->minimum(), -100);
    EXPECT_EQ(ui.lightnessSlider->maximum(), 100);
    EXPECT_EQ(ui.lightnessSlider->orientation(), Qt::Horizontal);
    EXPECT_EQ(ui.lightnessSlider->value(), 0);

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
TEST(AdjustHSLProgrammaticUiTest, HueSaturationLightnessControlsStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildAdjustHSLProcessUi(&host);

    QSliderSpinBox hueControl(ui.hueValue, ui.hueSlider, &host);
    QSignalSpy hueSpy(&hueControl, &QSliderSpinBox::valueChanged);

    ui.hueValue->setValue(90);
    QApplication::processEvents();
    EXPECT_GT(hueSpy.count(), 0);
    EXPECT_EQ(ui.hueSlider->value(), 90);

    ui.hueSlider->setValue(-45);
    QApplication::processEvents();
    EXPECT_EQ(ui.hueValue->value(), -45);

    QSliderSpinBox saturationControl(ui.saturationValue, ui.saturationSlider, &host);
    QSignalSpy saturationSpy(&saturationControl, &QSliderSpinBox::valueChanged);

    ui.saturationValue->setValue(25);
    QApplication::processEvents();
    EXPECT_GT(saturationSpy.count(), 0);
    EXPECT_EQ(ui.saturationSlider->value(), 25);

    ui.saturationSlider->setValue(-30);
    QApplication::processEvents();
    EXPECT_EQ(ui.saturationValue->value(), -30);

    QSliderSpinBox lightnessControl(ui.lightnessValue, ui.lightnessSlider, &host);
    QSignalSpy lightnessSpy(&lightnessControl, &QSliderSpinBox::valueChanged);

    ui.lightnessValue->setValue(40);
    QApplication::processEvents();
    EXPECT_GT(lightnessSpy.count(), 0);
    EXPECT_EQ(ui.lightnessSlider->value(), 40);

    ui.lightnessSlider->setValue(-35);
    QApplication::processEvents();
    EXPECT_EQ(ui.lightnessValue->value(), -35);
}

} // anonymous namespace
