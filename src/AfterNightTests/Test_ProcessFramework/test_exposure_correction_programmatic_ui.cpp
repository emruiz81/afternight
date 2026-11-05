// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Exposure Correction dialog as its builder makes it: the widgets the window reads with their ranges and defaults,
/// and the exposure spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/ExposureCorrectionProcessUiBuilder.h"

namespace
{

// The dialog opens with an exposure from -5 to 5 at zero in steps of 0.03, the preview on, the histogram grid, clipping
// figures and histogram shown, local normalization off, and Apply, Close and Reset.
TEST(ExposureCorrectionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildExposureCorrectionProcessUi(&host);

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
    ASSERT_NE(ui.exposureValue, nullptr);
    ASSERT_NE(ui.exposureSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Exposure Correction"));

    EXPECT_DOUBLE_EQ(ui.exposureValue->minimum(), -5.0);
    EXPECT_DOUBLE_EQ(ui.exposureValue->maximum(), 5.0);
    EXPECT_DOUBLE_EQ(ui.exposureValue->singleStep(), 0.03);
    EXPECT_DOUBLE_EQ(ui.exposureValue->value(), 0.0);

    EXPECT_EQ(ui.exposureSlider->minimum(), -5000);
    EXPECT_EQ(ui.exposureSlider->maximum(), 5000);
    EXPECT_EQ(ui.exposureSlider->orientation(), Qt::Horizontal);

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

// Bound as the window binds them, the exposure spin box and its slider move together across their ranges and a change
// of either is reported.
TEST(ExposureCorrectionProgrammaticUiTest, ExposureSpinAndSliderStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildExposureCorrectionProcessUi(&host);

    QSliderDoubleSpinBox exposureControl(ui.exposureValue, ui.exposureSlider, &host);
    QSignalSpy exposureSpy(&exposureControl, &QSliderDoubleSpinBox::valueChanged);

    ui.exposureValue->setValue(2.5);
    QApplication::processEvents();
    EXPECT_GT(exposureSpy.count(), 0);
    EXPECT_EQ(ui.exposureSlider->value(), 750);

    ui.exposureSlider->setValue(250);
    QApplication::processEvents();
    EXPECT_NEAR(ui.exposureValue->value(), -2.5, 0.01);
}

} // anonymous namespace
