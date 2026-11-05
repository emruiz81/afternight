// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Median Filter dialog as its builder makes it: the widgets the window reads with their ranges and defaults, and
/// each spin box and slider moving together once the window binds them.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QSlider>
#include <QWidget>

#include "controls/QSliderSpinBox.h"
#include "processes/MedianFilterProcessUiBuilder.h"

namespace
{

// The dialog opens with one to ten iterations at one, a modulation from 0 to 1 at one, the kernel list filled by the
// window, the preview on, and Apply, Close and Reset.
TEST(MedianFilterProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildMedianFilterProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.kernelSize, nullptr);
    ASSERT_NE(ui.iterationsValue, nullptr);
    ASSERT_NE(ui.iterationsSlider, nullptr);
    ASSERT_NE(ui.modulationValue, nullptr);
    ASSERT_NE(ui.modulationSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Median Filter"));

    EXPECT_EQ(ui.kernelSize->count(), 0);

    EXPECT_EQ(ui.iterationsValue->minimum(), 1);
    EXPECT_EQ(ui.iterationsValue->maximum(), 10);
    EXPECT_EQ(ui.iterationsValue->value(), 1);

    EXPECT_EQ(ui.iterationsSlider->minimum(), 1);
    EXPECT_EQ(ui.iterationsSlider->maximum(), 10);
    EXPECT_EQ(ui.iterationsSlider->orientation(), Qt::Horizontal);

    EXPECT_DOUBLE_EQ(ui.modulationValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.modulationValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.modulationValue->singleStep(), 0.05);
    EXPECT_DOUBLE_EQ(ui.modulationValue->value(), 1.0);

    EXPECT_EQ(ui.modulationSlider->minimum(), 0);
    EXPECT_EQ(ui.modulationSlider->maximum(), 100);
    EXPECT_EQ(ui.modulationSlider->value(), 100);
    EXPECT_EQ(ui.modulationSlider->orientation(), Qt::Horizontal);

    EXPECT_TRUE(ui.preview->isChecked());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// Bound as the window binds them, each spin box and its slider move together and a change of either is reported.
TEST(MedianFilterProgrammaticUiTest, IterationAndModulationControlsStayWired)
{
    QWidget host;
    auto ui = an::ui::processes::buildMedianFilterProcessUi(&host);

    QSliderSpinBox iterationsControl(ui.iterationsValue, ui.iterationsSlider, &host);
    QSignalSpy iterationsSpy(&iterationsControl, &QSliderSpinBox::valueChanged);

    ui.iterationsValue->setValue(6);
    QApplication::processEvents();
    EXPECT_GT(iterationsSpy.count(), 0);
    EXPECT_EQ(ui.iterationsSlider->value(), 6);

    ui.iterationsSlider->setValue(3);
    QApplication::processEvents();
    EXPECT_EQ(ui.iterationsValue->value(), 3);

    QSliderDoubleSpinBox modulationControl(ui.modulationValue, ui.modulationSlider, &host);
    QSignalSpy modulationSpy(&modulationControl, &QSliderDoubleSpinBox::valueChanged);

    ui.modulationValue->setValue(0.75);
    QApplication::processEvents();
    EXPECT_GT(modulationSpy.count(), 0);
    EXPECT_EQ(ui.modulationSlider->value(), 750);

    ui.modulationSlider->setValue(250);
    QApplication::processEvents();
    EXPECT_NEAR(ui.modulationValue->value(), 0.25, 0.01);
}

} // anonymous namespace
