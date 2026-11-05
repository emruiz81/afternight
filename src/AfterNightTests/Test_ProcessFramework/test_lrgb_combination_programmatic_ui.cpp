// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The LRGB Combination dialog as its builder makes it: the widgets the window reads, laid out with the theme's
/// spacing.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/LRGBCombinationProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

namespace
{

// The dialog opens at its size, laid out with the theme's spacing, the preview on, the histogram grid, clipping figures
// and histogram shown, local normalization off.
TEST(LRGBCombinationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    auto ui = an::ui::processes::buildLRGBCombinationProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.luminanceImageSelector, nullptr);
    ASSERT_NE(ui.rgbImageSelector, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("LRGB Combination"));
    EXPECT_EQ(host.size(), QSize(1000, 819));
    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->stretch(0), 0);
    EXPECT_EQ(ui.horizontalLayoutMain->stretch(1), -1);

    EXPECT_TRUE(ui.gridButton->isCheckable());
    EXPECT_TRUE(ui.gridButton->isChecked());
    EXPECT_TRUE(ui.showClippingInfo->isCheckable());
    EXPECT_TRUE(ui.showClippingInfo->isChecked());
    EXPECT_TRUE(ui.localChannelNormalization->isCheckable());
    EXPECT_FALSE(ui.localChannelNormalization->isChecked());
    EXPECT_TRUE(ui.showHistogram->isCheckable());
    EXPECT_TRUE(ui.showHistogram->isChecked());
    EXPECT_TRUE(ui.showHistogramAreas->isCheckable());
    EXPECT_FALSE(ui.showHistogramAreas->isChecked());

    EXPECT_EQ(ui.lightnessWeightValue->value(), 1.0);
    EXPECT_EQ(ui.redWeightValue->value(), 1.0);
    EXPECT_EQ(ui.greenWeightValue->value(), 1.0);
    EXPECT_EQ(ui.blueWeightValue->value(), 1.0);
    EXPECT_EQ(ui.lightnessMTFValue->value(), 0.5);
    EXPECT_EQ(ui.saturationMTFValue->value(), 0.5);

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

} // namespace
