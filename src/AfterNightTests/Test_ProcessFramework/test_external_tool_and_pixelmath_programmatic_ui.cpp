// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The external tool and PixelMath dialogs as their builders make them - StarNet, StarNet's star mask, DeepSNR and
/// PixelMath: the widgets each window reads, their rows laid out without overlapping at the dialog's smallest size, and
/// the unscreened star layer chosen for a starless view.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLayout>
#include <QWidget>

#include "processes/DeepSNRProcessUiBuilder.h"
#include "processes/PixelMathProcessUiBuilder.h"
#include "processes/StarnetProcessUiBuilder.h"
#include "processes/StarnetStarMaskProcessUiBuilder.h"

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

void activateTestLayout(QWidget& host)
{
    host.resize(host.minimumSize().expandedTo(host.minimumSizeHint()));
    if (auto* layout = host.layout())
    {
        layout->activate();
    }
    host.show();
    QApplication::processEvents();
}

void expectBelow(QWidget* lower, QWidget* upper)
{
    ASSERT_NE(lower, nullptr);
    ASSERT_NE(upper, nullptr);
    EXPECT_GT(lower->geometry().top(), upper->geometry().bottom());
}

// The StarNet dialog opens with its configuration, star output, stride, weights and options, and Ok and Close.
TEST(StarnetProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarnetProcessUi(&host);

    ASSERT_NE(ui.configureStarnet, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.starExtractionMode, nullptr);
    ASSERT_NE(ui.starOutputMode, nullptr);
    ASSERT_NE(ui.stridePreset, nullptr);
    ASSERT_NE(ui.customStrideValue, nullptr);
    ASSERT_NE(ui.generateStarMask, nullptr);
    ASSERT_NE(ui.upsamplex2, nullptr);
    ASSERT_NE(ui.highlightProtection, nullptr);
    ASSERT_NE(ui.weightsPath, nullptr);
    ASSERT_NE(ui.browseWeights, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Starnet++"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close, QDialogButtonBox::Ok});
}

// At the dialog's smallest size each row of the StarNet dialog lies below the one before it.
TEST(StarnetProgrammaticUiTest, ParameterRowsDoNotOverlapAtMinimumSize)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarnetProcessUi(&host);

    activateTestLayout(host);

    expectBelow(ui.stridePresetLabel, ui.starOutputMode);
    expectBelow(ui.stridePreset, ui.starOutputMode);
    expectBelow(ui.weightsPathLabel, ui.stridePreset);
    expectBelow(ui.weightsPath, ui.stridePreset);
}

// A starless view asked for by another process chooses the unscreened star layer, whatever the star output showed.
TEST(StarnetProgrammaticUiTest, IntegratedStarlessGenerationHelperSelectsUnscreenMode)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarnetProcessUi(&host);
    ASSERT_NE(ui.starExtractionMode, nullptr);

    ui.starExtractionMode->addItems({QStringLiteral("Additive"), QStringLiteral("Unscreen")});
    ui.starExtractionMode->setCurrentText(QStringLiteral("Additive"));

    an::ui::processes::selectStarnetUnscreenExtractionMode(ui);

    EXPECT_EQ(ui.starExtractionMode->currentText(), QStringLiteral("Unscreen"));
}

// The StarNet star mask dialog opens with its configuration, stride, weights and mask selection, and Ok and Close.
TEST(StarnetStarMaskProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarnetStarMaskProcessUi(&host);

    ASSERT_NE(ui.configureStarnet, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.stridePreset, nullptr);
    ASSERT_NE(ui.customStrideValue, nullptr);
    ASSERT_NE(ui.highlightProtection, nullptr);
    ASSERT_NE(ui.weightsPath, nullptr);
    ASSERT_NE(ui.browseWeights, nullptr);
    ASSERT_NE(ui.lightness, nullptr);
    ASSERT_NE(ui.binaryMask, nullptr);
    ASSERT_NE(ui.lowerLimitValue, nullptr);
    ASSERT_NE(ui.upperLimitValue, nullptr);
    ASSERT_NE(ui.smoothnessValue, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Starnet Starmask"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close, QDialogButtonBox::Ok});
}

// At the dialog's smallest size the StarNet star mask dialog's weights row lies below its stride.
TEST(StarnetStarMaskProgrammaticUiTest, ParameterRowsDoNotOverlapAtMinimumSize)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarnetStarMaskProcessUi(&host);

    activateTestLayout(host);

    expectBelow(ui.weightsPathLabel, ui.stridePreset);
    expectBelow(ui.weightsPath, ui.stridePreset);
}

// The DeepSNR dialog opens with its configuration, stride and weights and no pre-stretch of a linear image, and Ok and
// Close.
TEST(DeepSNRProgrammaticUiTest, ConstructorDefaultsMatchExpectedContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildDeepSNRProcessUi(&host);

    ASSERT_NE(ui.configureDeepSNR, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.stridePreset, nullptr);
    ASSERT_NE(ui.customStrideValue, nullptr);
    ASSERT_NE(ui.preStretchLinearImage, nullptr);
    ASSERT_NE(ui.weightsPath, nullptr);
    ASSERT_NE(ui.browseWeights, nullptr);

    EXPECT_EQ(ui.preStretchLinearImage->text(), QStringLiteral("Pre-Stretch linear image"));
    EXPECT_FALSE(ui.preStretchLinearImage->isChecked());
    EXPECT_EQ(host.windowTitle(), QStringLiteral("DeepSNR"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close, QDialogButtonBox::Ok});
}

// At the dialog's smallest size each row of the DeepSNR dialog lies below the one before it.
TEST(DeepSNRProgrammaticUiTest, ParameterRowsDoNotOverlapAtMinimumSize)
{
    QWidget host;
    auto ui = an::ui::processes::buildDeepSNRProcessUi(&host);

    activateTestLayout(host);

    expectBelow(ui.weightsPathLabel, ui.stridePreset);
    expectBelow(ui.weightsPath, ui.stridePreset);
    expectBelow(ui.targetImageLabel, ui.weightsPath);
    expectBelow(ui.targetImageSelector, ui.weightsPath);
}

// The PixelMath dialog opens with its expressions, rescale, output and colour space, and Apply and Close.
TEST(PixelMathProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildPixelMathProcessUi(&host);

    ASSERT_NE(ui.tabWidget, nullptr);
    ASSERT_NE(ui.useSingleRGBKExpression, nullptr);
    ASSERT_NE(ui.rescaleResult, nullptr);
    ASSERT_NE(ui.outputModeSelector, nullptr);
    ASSERT_NE(ui.colorSpaceSelector, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.rgbkTextEdit, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Form"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close});
}

} // anonymous namespace
