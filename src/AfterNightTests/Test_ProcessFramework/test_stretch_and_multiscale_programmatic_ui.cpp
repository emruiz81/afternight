// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stretching, morphological and multiscale dialogs as their builders make them - Curves Transformation, Histogram
/// Transformation, Morphological Transformation, Multiscale Linear Transform and Statistical Stretch: the widgets each
/// window reads, their titles and their buttons.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include "processes/CurvesTransformationProcessUiBuilder.h"
#include "processes/HistogramTransformationProcessUiBuilder.h"
#include "processes/MorphologicalTransformationProcessUiBuilder.h"
#include "processes/MultiscaleLinearProcessUiBuilder.h"
#include "processes/StatisticalStretchUiBuilder.h"

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>

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

// The Curves Transformation dialog opens with the widgets its window reads, and Apply, Close and Reset.
TEST(CurvesTransformationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildCurvesTransformationProcessUi(&host);

    ASSERT_NE(ui.interpolationSelector, nullptr);
    ASSERT_NE(ui.channelSelector2, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Curves Transformation"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Histogram Transformation dialog opens with the widgets its window reads, and Apply, Close and Reset.
TEST(HistogramTransformationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildHistogramTransformationProcessUi(&host);

    ASSERT_NE(ui.shadowsValue, nullptr);
    ASSERT_NE(ui.midtonesValue, nullptr);
    ASSERT_NE(ui.highlightsValue, nullptr);
    ASSERT_NE(ui.plotResolution, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Histogram Transformation"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Morphological Transformation dialog opens with its operator, iterations, amount and structuring element, and the
// selection rank Morphological Selection reads from 0 to 1 at the median, and Apply, Close and Reset.
TEST(MorphologicalTransformationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildMorphologicalTransformationProcessUi(&host);

    ASSERT_NE(ui.iterationsValue, nullptr);
    ASSERT_NE(ui.operatorSelector, nullptr);
    ASSERT_NE(ui.seSizeSelector, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Morphological Transformation"));
    ASSERT_NE(ui.selectionValue, nullptr);
    ASSERT_NE(ui.selectionSlider, nullptr);
    EXPECT_DOUBLE_EQ(ui.selectionValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.selectionValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.selectionValue->value(), 0.5);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Multiscale Linear Transform dialog opens with six layers, at most one fewer than the engine's transform holds,
// and Apply, Close and Reset.
TEST(MultiscaleLinearProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildMultiscaleLinearProcessUi(&host);

    ASSERT_NE(ui.algorithmComboBox, nullptr);
    ASSERT_NE(ui.numLayersSpinBox, nullptr);
    ASSERT_NE(ui.layerTreeWidget, nullptr);
    ASSERT_NE(ui.previewModeComboBox, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Multiscale Linear Transform"));
    EXPECT_EQ(ui.numLayersSpinBox->minimum(), 1);
    EXPECT_EQ(ui.numLayersSpinBox->maximum(), static_cast<int>(an::kMaxMultiscaleLayers) - 1);
    EXPECT_EQ(ui.numLayersSpinBox->value(), 6);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Statistical Stretch dialog opens with the widgets its window reads, and Apply, Close and Reset.
TEST(StatisticalStretchProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStatisticalStretchUi(&host);

    ASSERT_NE(ui.targetMedianValue, nullptr);
    ASSERT_NE(ui.curvesBoostValue, nullptr);
    ASSERT_NE(ui.linkedStretchCheck, nullptr);
    ASSERT_NE(ui.normalizeImageCheck, nullptr);
    ASSERT_NE(ui.applyCurvesCheck, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Statistical Stretch"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

} // anonymous namespace
