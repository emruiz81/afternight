// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Gamma Correction dialog as its builder makes it: the widgets the window reads with their ranges and defaults,
/// and the gamma spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/GammaCorrectionProcessUiBuilder.h"

namespace
{

// The dialog opens with a gamma from 0.01 to 9.9 at one, the preview on, the histogram grid, clipping figures and
// histogram shown, local normalization off, and Apply, Close and Reset.
TEST(GammaCorrectionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildGammaCorrectionProcessUi(&host);

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
    ASSERT_NE(ui.gammaValue, nullptr);
    ASSERT_NE(ui.gammaSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Gamma Correction"));

    EXPECT_DOUBLE_EQ(ui.gammaValue->minimum(), 0.01);
    EXPECT_DOUBLE_EQ(ui.gammaValue->maximum(), 9.9);
    EXPECT_DOUBLE_EQ(ui.gammaValue->singleStep(), 0.03);
    EXPECT_DOUBLE_EQ(ui.gammaValue->value(), 1.0);

    EXPECT_EQ(ui.gammaSlider->minimum(), 0);
    EXPECT_EQ(ui.gammaSlider->maximum(), 1000);
    EXPECT_EQ(ui.gammaSlider->orientation(), Qt::Horizontal);

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

// Bound as the window binds them, the gamma spin box and its slider move together and a change of either is reported.
TEST(GammaCorrectionProgrammaticUiTest, GammaSpinAndSliderStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildGammaCorrectionProcessUi(&host);

    QSliderDoubleSpinBox gammaControl(ui.gammaValue, ui.gammaSlider, &host);
    QSignalSpy gammaSpy(&gammaControl, &QSliderDoubleSpinBox::valueChanged);

    ui.gammaValue->setValue(2.5);
    QApplication::processEvents();
    EXPECT_GT(gammaSpy.count(), 0);
    EXPECT_GE(ui.gammaSlider->value(), 240);
    EXPECT_LE(ui.gammaSlider->value(), 260);

    ui.gammaSlider->setValue(700);
    QApplication::processEvents();
    EXPECT_NEAR(ui.gammaValue->value(), 6.93, 0.06);
}

} // anonymous namespace
