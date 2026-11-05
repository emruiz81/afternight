// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Blend dialog as its builder makes it: the widgets the window reads with their defaults, laid out with the
/// theme's spacing, and the dialog made and let go repeatedly.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>
#include <memory>

#include "processes/ImageBlendProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

namespace
{

// The dialog opens at full opacity with its filter at a radius of ten, full saturation and no hue shift, laid out with
// the theme's small spacing, the preview on, the histogram grid, clipping figures and histogram shown, local
// normalization off, and Ok, Close and Reset.
TEST(ImageBlendProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    auto ui = an::ui::processes::buildImageBlendProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.secondImageSelector, nullptr);
    ASSERT_NE(ui.blendMode, nullptr);
    ASSERT_NE(ui.filterSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Blend"));
    EXPECT_EQ(host.size(), QSize(800, 700));
    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.sm);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.sm);
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

    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_EQ(ui.opacityValue->value(), 1.0);
    EXPECT_EQ(ui.filterRadiusValue->value(), 10.0);
    EXPECT_EQ(ui.filterSaturationValue->value(), 1.0);
    EXPECT_EQ(ui.filterHueValue->value(), 0.0);

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Ok), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The dialog can be made and let go again and again, each time whole.
TEST(ImageBlendProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildImageBlendProcessUi(host.get());
        ASSERT_NE(ui.targetImageSelector, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

} // anonymous namespace
