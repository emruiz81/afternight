// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The noise reduction dialogs as their builders make them - GRAYCstoration, Non Local Bayes, Patch PCA Denoise, Total
/// Variation Denoise and Wavelet Denoise - with the widgets each window reads at their defaults, and the Non Local
/// Bayes run's own choices once the Anscombe transform is asked for.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/GRAYCstorationProcessUiBuilder.h"
#include "processes/NLBayesProcess.h"
#include "processes/NLBayesProcessUiBuilder.h"
#include "processes/PatchPCADenoiseProcessUiBuilder.h"
#include "processes/TotalVariationDenoiseProcessUiBuilder.h"
#include "processes/WaveletDenoiseProcessUiBuilder.h"

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

// The GRAYCstoration dialog opens at its size on one iteration at an amplitude of 60, a sharpness of 0.7, a noise scale
// of 0.6 and a regularity of 1.1, no fast approximation, the preview on.
TEST(GRAYCstorationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildGRAYCstorationProcessUi(&host);

    ASSERT_NE(ui.iterationsValue, nullptr);
    ASSERT_NE(ui.amplitudeValue, nullptr);
    ASSERT_NE(ui.sharpnessValue, nullptr);
    ASSERT_NE(ui.noiseScaleValue, nullptr);
    ASSERT_NE(ui.regularityValue, nullptr);
    ASSERT_NE(ui.fastApproximation, nullptr);
    ASSERT_NE(ui.interpolationSelector, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.size(), QSize(989, 577));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("GRAYCstoration"));
    EXPECT_EQ(ui.iterationsValue->value(), 1);
    EXPECT_EQ(ui.amplitudeValue->value(), 60);
    EXPECT_DOUBLE_EQ(ui.sharpnessValue->value(), 0.7);
    EXPECT_DOUBLE_EQ(ui.noiseScaleValue->value(), 0.6);
    EXPECT_DOUBLE_EQ(ui.regularityValue->value(), 1.1);
    EXPECT_FALSE(ui.fastApproximation->isChecked());
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Non Local Bayes dialog opens on the first area with its sigma found automatically, one second-order pass at 0.8,
// no DA3D and no Anscombe transform, at full amount, the preview on.
TEST(NLBayesProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildNLBayesProcessUi(&host);

    ASSERT_NE(ui.sigmaValue, nullptr);
    ASSERT_NE(ui.useArea1, nullptr);
    ASSERT_NE(ui.useArea2, nullptr);
    ASSERT_NE(ui.da3d, nullptr);
    ASSERT_NE(ui.anscombe, nullptr);
    ASSERT_NE(ui.autoSigma, nullptr);
    ASSERT_NE(ui.sosIterations, nullptr);
    ASSERT_NE(ui.sosRhoValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Non Local Bayes"));
    EXPECT_DOUBLE_EQ(ui.sigmaValue->value(), 5.0);
    EXPECT_TRUE(ui.useArea1->isChecked());
    EXPECT_FALSE(ui.useArea2->isChecked());
    EXPECT_FALSE(ui.da3d->isChecked());
    EXPECT_FALSE(ui.anscombe->isChecked());
    EXPECT_TRUE(ui.autoSigma->isChecked());
    EXPECT_EQ(ui.sosIterations->value(), 1);
    EXPECT_DOUBLE_EQ(ui.sosRhoValue->value(), 0.8);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// A run asked for under the Anscombe transform drops DA3D and runs one second-order pass, and keeps every other choice
// as it was asked for.
TEST(NLBayesProgrammaticUiTest, AnscombeExecutionSanitizationDisablesConflictingOptions)
{
    NLBayesParams params;
    params.useArea1 = true;
    params.useArea2 = true;
    params.da3d = true;
    params.anscombe = true;
    params.autoSigma = false;
    params.sigma = 4.5;
    params.amount = 0.75;
    params.sosIterations = 4;
    params.sosRho = 0.9;

    const NLBayesParams effective = NLBayesProcess::sanitizeParamsForExecution(params);

    EXPECT_TRUE(effective.useArea1);
    EXPECT_TRUE(effective.useArea2);
    EXPECT_FALSE(effective.da3d);
    EXPECT_TRUE(effective.anscombe);
    EXPECT_DOUBLE_EQ(effective.sigma, 4.5);
    EXPECT_DOUBLE_EQ(effective.amount, 0.75);
    EXPECT_EQ(effective.sosIterations, 1u);
    EXPECT_DOUBLE_EQ(effective.sosRho, 0.9);
}

// The Patch PCA Denoise dialog opens at a strength of 1.8 over seven-pixel patches, an eight-pixel search and a spatial
// sampling of seven, the preview on.
TEST(PatchPCADenoiseProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildPatchPCADenoiseProcessUi(&host);

    ASSERT_NE(ui.strenghtValue, nullptr);
    ASSERT_NE(ui.patchSizeValue, nullptr);
    ASSERT_NE(ui.lookupSizeValue, nullptr);
    ASSERT_NE(ui.spatialSamplingValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Patch PCA Denoise"));
    EXPECT_DOUBLE_EQ(ui.strenghtValue->value(), 1.8);
    EXPECT_EQ(ui.patchSizeValue->value(), 7);
    EXPECT_EQ(ui.lookupSizeValue->value(), 8);
    EXPECT_EQ(ui.spatialSamplingValue->value(), 7);
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Total Variation Denoise dialog opens isotropic at a weight of 0.5, an epsilon of 0.001 and a hundred iterations
// at most, the preview on.
TEST(TotalVariationDenoiseProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildTotalVariationDenoiseProcessUi(&host);

    ASSERT_NE(ui.algorithmSelector, nullptr);
    ASSERT_NE(ui.weightValue, nullptr);
    ASSERT_NE(ui.epsValue, nullptr);
    ASSERT_NE(ui.maxIterationsValue, nullptr);
    ASSERT_NE(ui.isotropic, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Total Variation Denoise"));
    EXPECT_DOUBLE_EQ(ui.weightValue->value(), 0.5);
    EXPECT_DOUBLE_EQ(ui.epsValue->value(), 0.001);
    EXPECT_EQ(ui.maxIterationsValue->value(), 100);
    EXPECT_TRUE(ui.isotropic->isChecked());
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Wavelet Denoise dialog opens at a threshold of 1.4 over six scales and ten iterations at full amount, the preview
// on.
TEST(WaveletDenoiseProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildWaveletDenoiseProcessUi(&host);

    ASSERT_NE(ui.thresholdValue, nullptr);
    ASSERT_NE(ui.scalesValue, nullptr);
    ASSERT_NE(ui.iterationsValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Wavelet Denoise"));
    EXPECT_DOUBLE_EQ(ui.thresholdValue->value(), 1.4);
    EXPECT_EQ(ui.scalesValue->value(), 6);
    EXPECT_EQ(ui.iterationsValue->value(), 10);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    EXPECT_TRUE(ui.preview->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

} // anonymous namespace
