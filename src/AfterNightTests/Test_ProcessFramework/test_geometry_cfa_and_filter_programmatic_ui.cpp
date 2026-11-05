// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry, filter-array and filter dialogs as their builders make them - Bilateral Filter, Binarize, CFA Integer
/// Resample, Colorize, Crop, Integer Resample, Merge CFA, Noise Generator, Resize, Rotate, Split CFA and Tighten Edges:
/// the widgets each window reads with their defaults, their sizes and their buttons.

#include <gtest/gtest.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QWidget>

#include "processes/BilateralFilterProcessUiBuilder.h"
#include "processes/BinarizeProcessUiBuilder.h"
#include "processes/CFAIntegerResampleProcessUiBuilder.h"
#include "processes/ColorizeProcessUiBuilder.h"
#include "processes/CropProcessUiBuilder.h"
#include "processes/IntegerResampleProcessUiBuilder.h"
#include "processes/MergeCFAProcessUiBuilder.h"
#include "processes/NoiseGeneratorProcessUiBuilder.h"
#include "processes/ResizeProcessUiBuilder.h"
#include "processes/RotateProcessUiBuilder.h"
#include "processes/SplitCFAProcessUiBuilder.h"
#include "processes/TightenEdgesProcessUiBuilder.h"

namespace
{

void expectButtonSet(QDialogButtonBox* button_box, std::initializer_list<QDialogButtonBox::StandardButton> buttons)
{
    ASSERT_NE(button_box, nullptr);
    for (const auto button : buttons)
    {
        EXPECT_NE(button_box->button(button), nullptr);
    }
}

// The Bilateral Filter dialog opens with its sigmas unlinked, the preview on, and Apply, Close and Reset.
TEST(BilateralFilterProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildBilateralFilterProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.linkSigmaControls, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Bilateral Filter"));
    EXPECT_FALSE(ui.linkSigmaControls->isChecked());
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Binarize dialog opens on one threshold for every channel, the green and blue ones out of use, the preview on, and
// Apply, Close and Reset.
TEST(BinarizeProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildBinarizeProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.selectionMode, nullptr);
    ASSERT_NE(ui.rgbkValue, nullptr);
    ASSERT_NE(ui.greenValue, nullptr);
    ASSERT_NE(ui.blueValue, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Binarize"));
    EXPECT_EQ(ui.selectionMode->count(), 2);
    EXPECT_FALSE(ui.greenValue->isEnabled());
    EXPECT_FALSE(ui.blueValue->isEnabled());
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The CFA Integer Resample dialog opens at a fixed size on downsampling by a factor of two to a hundred, with Ok, Close
// and Reset.
TEST(CFAIntegerResampleProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildCFAIntegerResampleProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.resampleFactor, nullptr);
    ASSERT_NE(ui.downSampleRadioButton, nullptr);
    ASSERT_NE(ui.UpsampleRadioButton, nullptr);
    ASSERT_NE(ui.downsampleMode, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(400, 390));
    EXPECT_EQ(host.maximumSize(), QSize(400, 390));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("CFA Integer Resample"));
    EXPECT_TRUE(ui.downSampleRadioButton->isChecked());
    EXPECT_FALSE(ui.UpsampleRadioButton->isChecked());
    EXPECT_EQ(ui.resampleFactor->minimum(), 2);
    EXPECT_EQ(ui.resampleFactor->maximum(), 100);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Colorize dialog opens with its method, hue and saturation, lightness unused, the preview on, and Apply, Close and
// Reset.
TEST(ColorizeProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildColorizeProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.colorizeMethod, nullptr);
    ASSERT_NE(ui.hueValue, nullptr);
    ASSERT_NE(ui.saturationValue, nullptr);
    ASSERT_NE(ui.useLightness, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Colorize"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_FALSE(ui.useLightness->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Crop dialog opens at a fixed width with a checkable New Selection button tall enough to be seen, and Ok, Close
// and Reset.
TEST(CropProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildCropProcessUi(&host);

    ASSERT_NE(ui.newSelectionButton, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.targetPosX, nullptr);
    ASSERT_NE(ui.targetPosY, nullptr);
    ASSERT_NE(ui.targetWidth, nullptr);
    ASSERT_NE(ui.targetHeight, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    // the layout constrains the width to 360, height may grow once we make the
    // "New Selection" button taller – assert the width stays fixed and the
    // height is at least 220.
    QSize minsz = host.minimumSize();
    EXPECT_EQ(minsz.width(), 360);
    EXPECT_GE(minsz.height(), 220);
    QSize maxsz = host.maximumSize();
    EXPECT_EQ(maxsz.width(), 360);
    EXPECT_GE(maxsz.height(), minsz.height());
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Crop"));
    EXPECT_TRUE(ui.newSelectionButton->isCheckable());
    // new button should be tall enough to attract attention
    EXPECT_GE(ui.newSelectionButton->minimumHeight(), 30);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Integer Resample dialog opens at a fixed size on downsampling by a factor of two to a hundred, with Ok, Close and
// Reset.
TEST(IntegerResampleProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildIntegerResampleProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.resampleFactor, nullptr);
    ASSERT_NE(ui.downSampleRadioButton, nullptr);
    ASSERT_NE(ui.UpsampleRadioButton, nullptr);
    ASSERT_NE(ui.downsampleMode, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(400, 390));
    EXPECT_EQ(host.maximumSize(), QSize(400, 390));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Integer Resample"));
    EXPECT_TRUE(ui.downSampleRadioButton->isChecked());
    EXPECT_FALSE(ui.UpsampleRadioButton->isChecked());
    EXPECT_EQ(ui.resampleFactor->minimum(), 2);
    EXPECT_EQ(ui.resampleFactor->maximum(), 100);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Merge CFA dialog opens with its pattern and four plane selectors, and Ok and Close.
TEST(MergeCFAProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildMergeCFAProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.patternSelector, nullptr);
    ASSERT_NE(ui.cfa0Selector, nullptr);
    ASSERT_NE(ui.cfa1Selector, nullptr);
    ASSERT_NE(ui.cfa2Selector, nullptr);
    ASSERT_NE(ui.cfa3Selector, nullptr);

    EXPECT_GE(host.minimumSize().width(), 380);
    EXPECT_GE(host.minimumSize().height(), 220);
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Merge CFA"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close});
}

// The Noise Generator dialog opens on Gaussian noise - its mean and variance in use, the salt and pepper amounts out of
// use - with the preview on, and Apply, Close and Reset.
TEST(NoiseGeneratorProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildNoiseGeneratorProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.noiseTypeSelector, nullptr);
    ASSERT_NE(ui.meanValue, nullptr);
    ASSERT_NE(ui.varValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.saltVsPepperValue, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Noise Generator"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_TRUE(ui.meanValue->isEnabled());
    EXPECT_TRUE(ui.varValue->isEnabled());
    EXPECT_FALSE(ui.amountValue->isEnabled());
    EXPECT_FALSE(ui.saltVsPepperValue->isEnabled());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Resize dialog opens at a fixed size keeping the aspect ratio at a hundred percent, with Ok, Close and Reset.
TEST(ResizeProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildResizeProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.keepAspectRatio, nullptr);
    ASSERT_NE(ui.targetWidth, nullptr);
    ASSERT_NE(ui.targetHeight, nullptr);
    ASSERT_NE(ui.targetWidthPercent, nullptr);
    ASSERT_NE(ui.targetHeightPercent, nullptr);
    ASSERT_NE(ui.interpolation, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(400, 310));
    EXPECT_EQ(host.maximumSize(), QSize(400, 310));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Resize"));
    EXPECT_TRUE(ui.keepAspectRatio->isChecked());
    EXPECT_DOUBLE_EQ(ui.targetWidthPercent->value(), 100.0);
    EXPECT_DOUBLE_EQ(ui.targetHeightPercent->value(), 100.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Rotate dialog opens at a fixed size at no angle, the image not resized, with Ok, Close and Reset.
TEST(RotateProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildRotateProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.groupBox, nullptr);
    ASSERT_NE(ui.resizeImage, nullptr);
    ASSERT_NE(ui.rotationAngle, nullptr);
    ASSERT_NE(ui.rotationAngleSlider, nullptr);
    ASSERT_NE(ui.interpolation, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(382, 360));
    EXPECT_EQ(host.maximumSize(), QSize(382, 360));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Rotate"));
    EXPECT_FALSE(ui.groupBox->isChecked());
    EXPECT_FALSE(ui.resizeImage->isChecked());
    EXPECT_DOUBLE_EQ(ui.rotationAngle->value(), 0.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Split CFA dialog opens at a fixed size with its method and resampling, untitled until the window titles itself
// after its target, and Ok and Close.
TEST(SplitCFAProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildSplitCFAProcessUi(&host);

    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.methodSelector, nullptr);
    ASSERT_NE(ui.haOIIIResamplingSelector, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_GE(host.minimumSize().width(), 200);
    EXPECT_GE(host.minimumSize().height(), 150);
    // Fixed-size: min == max after adjustSize + setFixedSize
    EXPECT_EQ(host.minimumSize(), host.maximumSize());
    EXPECT_TRUE(host.windowTitle().isEmpty()) << "the window titles itself after the target it splits";
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close});
}

// The Tighten Edges dialog opens with no tightening at full amount, the preview on, and Apply, Close and Reset.
TEST(TightenEdgesProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildTightenEdgesProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.valueValue, nullptr);
    ASSERT_NE(ui.valueSlider, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.amountSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Tighten Edges"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_DOUBLE_EQ(ui.valueValue->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

} // anonymous namespace
