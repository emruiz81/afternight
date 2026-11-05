// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The calibration, background and selection dialogs as their builders make them - Background Extraction, Background
/// Neutralization, Color Calibration, Debayer, Range Selection, Linear Match and Cosmetic Correction: the widgets each
/// window reads with their defaults and ranges, their sizes and their buttons.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QWidget>

#include "processes/BackgroundExtractionProcessUiBuilder.h"
#include "processes/BackgroundNeutralizationProcessUiBuilder.h"
#include "processes/ColorCalibrationProcessUiBuilder.h"
#include "processes/CosmeticCorrectionProcessUiBuilder.h"
#include "processes/DebayerProcessUiBuilder.h"
#include "processes/LinearMatchProcessUiBuilder.h"
#include "processes/RangeSelectionProcessUiBuilder.h"

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

// The Background Extraction dialog opens with a smoothing of 0.5, twenty samples a line and a grid tolerance of two,
// and Apply, Close and Reset.
TEST(BackgroundExtractionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildBackgroundExtractionProcessUi(&host);

    ASSERT_NE(ui.interpolationMethod, nullptr);
    ASSERT_NE(ui.correctionMode, nullptr);
    ASSERT_NE(ui.previewButton, nullptr);
    ASSERT_NE(ui.smoothingValue, nullptr);
    ASSERT_NE(ui.samplesPerLineValue, nullptr);
    ASSERT_NE(ui.gridToleranceValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_GE(host.minimumSize().width(), 600);
    EXPECT_GE(host.minimumSize().height(), 420);
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Background Extraction"));
    EXPECT_DOUBLE_EQ(ui.smoothingValue->value(), 0.5);
    EXPECT_EQ(ui.samplesPerLineValue->value(), 20);
    EXPECT_DOUBLE_EQ(ui.gridToleranceValue->value(), 2.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Background Neutralization dialog opens at a fixed size measuring the background between 0 and 0.1 over the whole
// reference, its region unchecked, with Ok, Cancel and Reset.
TEST(BackgroundNeutralizationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildBackgroundNeutralizationProcessUi(&host);

    ASSERT_NE(ui.backgroundReferenceImageSelector, nullptr);
    ASSERT_NE(ui.backgroundLowerLimitValue, nullptr);
    ASSERT_NE(ui.backgroundUpperLimitValue, nullptr);
    ASSERT_NE(ui.backgroundRoiGroupBox, nullptr);
    ASSERT_NE(ui.backgroundRoiSelectionButton, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(500, 360));
    EXPECT_EQ(host.maximumSize(), QSize(500, 360));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Background Neutralization"));
    EXPECT_DOUBLE_EQ(ui.backgroundLowerLimitValue->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.backgroundUpperLimitValue->value(), 0.1);
    EXPECT_TRUE(ui.backgroundRoiGroupBox->isCheckable());
    EXPECT_FALSE(ui.backgroundRoiGroupBox->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Cancel, QDialogButtonBox::Reset});
}

// The Color Calibration dialog opens as a resizable window measuring the white up to 0.9 and the background up to 0.1
// over their whole references, no manual white balance - whose values cannot fall to zero - and Apply, Close and Reset.
TEST(ColorCalibrationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildColorCalibrationProcessUi(&host);

    ASSERT_NE(ui.whiteReferenceImageSelector, nullptr);
    ASSERT_NE(ui.backgroundReferenceImageSelector, nullptr);
    ASSERT_NE(ui.whiteLowerLimitValue, nullptr);
    ASSERT_NE(ui.whiteUpperLimitValue, nullptr);
    ASSERT_NE(ui.backgroundLowerLimitValue, nullptr);
    ASSERT_NE(ui.backgroundUpperLimitValue, nullptr);
    ASSERT_NE(ui.mwbGroupBox, nullptr);
    ASSERT_NE(ui.whiteRoiGroupBox, nullptr);
    ASSERT_NE(ui.whiteRoiSelectionButton, nullptr);
    ASSERT_NE(ui.backgroundRoiGroupBox, nullptr);
    ASSERT_NE(ui.backgroundRoiSelectionButton, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    // A resizable window with a minimum size floor.
    EXPECT_GE(host.minimumWidth(), 500);
    EXPECT_GE(host.minimumHeight(), 500);
    EXPECT_EQ(host.maximumSize(), QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Color Calibration"));
    EXPECT_DOUBLE_EQ(ui.whiteUpperLimitValue->value(), 0.9);
    EXPECT_DOUBLE_EQ(ui.backgroundUpperLimitValue->value(), 0.1);
    EXPECT_FALSE(ui.mwbGroupBox->isChecked());
    for (const QDoubleSpinBox* white : {ui.mwbRedValue, ui.mwbGreenValue, ui.mwbBlueValue})
    {
        ASSERT_NE(white, nullptr);
        EXPECT_GT(white->minimum(), 0.0);
        EXPECT_DOUBLE_EQ(white->maximum(), 1.0);
        EXPECT_DOUBLE_EQ(white->value(), 1.0);
    }
    EXPECT_TRUE(ui.whiteRoiGroupBox->isCheckable());
    EXPECT_FALSE(ui.whiteRoiGroupBox->isChecked());
    EXPECT_TRUE(ui.backgroundRoiGroupBox->isCheckable());
    EXPECT_FALSE(ui.backgroundRoiGroupBox->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Debayer dialog opens at a fixed size with its pattern, method and output, and Ok and Close.
TEST(DebayerProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildDebayerProcessUi(&host);

    ASSERT_NE(ui.bayerPattern, nullptr);
    ASSERT_NE(ui.demosaicingMethod, nullptr);
    ASSERT_NE(ui.outputMode, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_GE(host.minimumSize().width(), 300);
    EXPECT_GE(host.minimumSize().height(), 200);
    EXPECT_EQ(host.minimumSize(), host.maximumSize());
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Debayer"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close});
}

// The Range Selection dialog opens selecting up to one on the lightness, not screened, not inverted and unlinked, its
// smoothness held to the 32 pixels the exact Gaussian reaches, the preview on, and Ok, Cancel and Reset.
TEST(RangeSelectionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildRangeSelectionProcessUi(&host);

    ASSERT_NE(ui.lowerLimitValue, nullptr);
    ASSERT_NE(ui.upperLimitValue, nullptr);
    ASSERT_NE(ui.fuzzinessValue, nullptr);
    ASSERT_NE(ui.smoothnessValue, nullptr);
    ASSERT_NE(ui.screening, nullptr);
    ASSERT_NE(ui.lightness, nullptr);
    ASSERT_NE(ui.invert, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.size(), QSize(898, 415));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Range Selection"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_FALSE(ui.linkRangeLimits->isEnabled());
    EXPECT_DOUBLE_EQ(ui.upperLimitValue->value(), 1.0);
    EXPECT_FALSE(ui.screening->isChecked());
    EXPECT_TRUE(ui.lightness->isChecked());
    EXPECT_DOUBLE_EQ(ui.smoothnessValue->maximum(), 32.0);
    EXPECT_EQ(ui.smoothnessSlider->maximum(), 320);
    EXPECT_FALSE(ui.invert->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Cancel, QDialogButtonBox::Reset});
}

// The Linear Match dialog opens at a fixed size rejecting above 0.92, with Ok and Close.
TEST(LinearMatchProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildLinearMatchProcessUi(&host);

    ASSERT_NE(ui.referenceImageSelector, nullptr);
    ASSERT_NE(ui.rejectLowValue, nullptr);
    ASSERT_NE(ui.rejectHighValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(460, 280));
    EXPECT_EQ(host.maximumSize(), QSize(460, 280));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Linear Match"));
    EXPECT_DOUBLE_EQ(ui.rejectHighValue->value(), 0.92);
    EXPECT_EQ(ui.rejectHighSlider->value(), 920);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Ok, QDialogButtonBox::Close});
}

// The Cosmetic Correction dialog opens with hot and cold pixels thresholded at 0.5 at full amount, the preview on, and
// Apply, Close and Reset.
TEST(CosmeticCorrectionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildCosmeticCorrectionProcessUi(&host);

    ASSERT_NE(ui.hotPixelsGroupBox, nullptr);
    ASSERT_NE(ui.coldPixelsGroupBox, nullptr);
    ASSERT_NE(ui.kernelSize, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Cosmetic Correction"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_TRUE(ui.hotPixelsGroupBox->isCheckable());
    EXPECT_TRUE(ui.coldPixelsGroupBox->isCheckable());
    EXPECT_DOUBLE_EQ(ui.hotPixelsThresholdValue->value(), 0.5);
    EXPECT_DOUBLE_EQ(ui.coldPixelsThresholdValue->value(), 0.5);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

} // anonymous namespace
