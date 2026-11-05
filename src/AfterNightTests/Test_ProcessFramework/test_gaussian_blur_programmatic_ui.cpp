// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Gaussian Blur dialog as its builder makes it: the sigma the window reads with its range and default, and the
/// sigma spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/GaussianBlurProcessUiBuilder.h"

namespace
{

// The dialog opens with a sigma from 0 to 100 at zero in steps of 0.5, the preview on, and Apply, Close and Reset.
TEST(GaussianBlurProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildGaussianBlurProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.sigmaValue, nullptr);
    ASSERT_NE(ui.sigmaSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(0, 0));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Gaussian Blur"));

    EXPECT_DOUBLE_EQ(ui.sigmaValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.sigmaValue->maximum(), 100.0);
    EXPECT_DOUBLE_EQ(ui.sigmaValue->singleStep(), 0.5);
    EXPECT_DOUBLE_EQ(ui.sigmaValue->value(), 0.0);

    EXPECT_EQ(ui.sigmaSlider->minimum(), 0);
    EXPECT_EQ(ui.sigmaSlider->maximum(), 1000);
    EXPECT_EQ(ui.sigmaSlider->orientation(), Qt::Horizontal);
    EXPECT_EQ(ui.sigmaSlider->value(), 0);

    EXPECT_TRUE(ui.preview->isChecked());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// Bound as the window binds them, the sigma spin box and its slider move together and a change of either is reported.
TEST(GaussianBlurProgrammaticUiTest, SigmaSpinAndSliderStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildGaussianBlurProcessUi(&host);

    QSliderDoubleSpinBox sigmaControl(ui.sigmaValue, ui.sigmaSlider, &host);
    QSignalSpy sigmaSpy(&sigmaControl, &QSliderDoubleSpinBox::valueChanged);

    ui.sigmaValue->setValue(12.5);
    QApplication::processEvents();
    EXPECT_GT(sigmaSpy.count(), 0);
    EXPECT_EQ(ui.sigmaSlider->value(), 125);

    ui.sigmaSlider->setValue(250);
    QApplication::processEvents();
    EXPECT_NEAR(ui.sigmaValue->value(), 25.0, 0.2);
}

} // anonymous namespace
