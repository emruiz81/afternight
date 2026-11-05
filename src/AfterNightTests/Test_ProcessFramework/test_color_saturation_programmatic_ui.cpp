// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Color Saturation dialog as its builder makes it: the widgets the window reads with their ranges and defaults,
/// and each spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/ColorSaturationProcessUiBuilder.h"

namespace
{

// The dialog opens with an amount from -1 to 1 at zero and a background factor from 0 to 2 at one, the hue list filled
// by the window, the preview on, the histogram grid, clipping figures and histogram shown, local normalization off, and
// Apply, Close and Reset.
TEST(ColorSaturationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildColorSaturationProcessUi(&host);

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
    ASSERT_NE(ui.hueSelector, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.amountSlider, nullptr);
    ASSERT_NE(ui.bgFactorValue, nullptr);
    ASSERT_NE(ui.bgFactorSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Color Saturation"));

    EXPECT_EQ(ui.hueSelector->count(), 0);

    EXPECT_DOUBLE_EQ(ui.amountValue->minimum(), -1.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->singleStep(), 0.01);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 0.0);

    EXPECT_EQ(ui.amountSlider->minimum(), -1000);
    EXPECT_EQ(ui.amountSlider->maximum(), 1000);
    EXPECT_EQ(ui.amountSlider->orientation(), Qt::Horizontal);

    EXPECT_DOUBLE_EQ(ui.bgFactorValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.bgFactorValue->maximum(), 2.0);
    EXPECT_DOUBLE_EQ(ui.bgFactorValue->singleStep(), 0.01);
    EXPECT_DOUBLE_EQ(ui.bgFactorValue->value(), 1.0);

    EXPECT_EQ(ui.bgFactorSlider->minimum(), 0);
    EXPECT_EQ(ui.bgFactorSlider->maximum(), 2000);
    EXPECT_EQ(ui.bgFactorSlider->value(), 1000);
    EXPECT_EQ(ui.bgFactorSlider->orientation(), Qt::Horizontal);

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

// Bound as the window binds them, each spin box and its slider move together across their ranges and a change of either
// is reported.
TEST(ColorSaturationProgrammaticUiTest, AmountAndBackgroundControlsStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildColorSaturationProcessUi(&host);

    QSliderDoubleSpinBox amountControl(ui.amountValue, ui.amountSlider, &host);
    QSignalSpy amountSpy(&amountControl, &QSliderDoubleSpinBox::valueChanged);

    ui.amountValue->setValue(0.42);
    QApplication::processEvents();
    EXPECT_GT(amountSpy.count(), 0);
    EXPECT_EQ(ui.amountSlider->value(), 710);

    ui.amountSlider->setValue(375);
    QApplication::processEvents();
    EXPECT_NEAR(ui.amountValue->value(), -0.25, 0.01);

    QSliderDoubleSpinBox bgFactorControl(ui.bgFactorValue, ui.bgFactorSlider, &host);
    QSignalSpy bgFactorSpy(&bgFactorControl, &QSliderDoubleSpinBox::valueChanged);

    ui.bgFactorValue->setValue(1.5);
    QApplication::processEvents();
    EXPECT_GT(bgFactorSpy.count(), 0);
    EXPECT_EQ(ui.bgFactorSlider->value(), 750);

    ui.bgFactorSlider->setValue(125);
    QApplication::processEvents();
    EXPECT_NEAR(ui.bgFactorValue->value(), 0.25, 0.01);
}

} // anonymous namespace
