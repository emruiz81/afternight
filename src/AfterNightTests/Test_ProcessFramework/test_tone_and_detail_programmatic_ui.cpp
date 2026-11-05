// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The tone and detail dialogs as their builders make them - Adjust LCh, ArcSinh Stretch, Banding Reduction, CLAHE,
/// High Pass Filter, SCNR and Unsharp Mask: the widgets each window reads with their defaults, their sizes and their
/// buttons.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/AdjustLChProcessUiBuilder.h"
#include "processes/ArcSinhStretchProcessUiBuilder.h"
#include "processes/BandingReductionProcessUiBuilder.h"
#include "processes/CLAHEProcessUiBuilder.h"
#include "processes/HighPassFilterProcessUiBuilder.h"
#include "processes/SCNRProcessUiBuilder.h"
#include "processes/UnsharpMaskProcessUiBuilder.h"

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

// The Adjust LCh dialog opens with the preview, the histogram grid, clipping figures and histogram on, and Apply, Close
// and Reset.
TEST(AdjustLChProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildAdjustLChProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.hueValue, nullptr);
    ASSERT_NE(ui.chromaValue, nullptr);
    ASSERT_NE(ui.lightnessValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Adjust LCh"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_TRUE(ui.gridButton->isChecked());
    EXPECT_TRUE(ui.showClippingInfo->isChecked());
    EXPECT_TRUE(ui.showHistogram->isChecked());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The ArcSinh Stretch dialog opens with no stretch and no black point over a human-weighted luminance, the preview on,
// and Apply, Close and Reset.
TEST(ArcSinhStretchProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildArcSinhStretchProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.stretchFactor, nullptr);
    ASSERT_NE(ui.blackPoint, nullptr);
    ASSERT_NE(ui.humanWeightedLuminance, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("ArcSinh Stretch"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_TRUE(ui.humanWeightedLuminance->isChecked());
    EXPECT_DOUBLE_EQ(ui.stretchFactor->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.blackPoint->value(), 0.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Banding Reduction dialog opens on horizontal banding at full amount and one sigma, highlights not protected, the
// preview on, and Apply, Close and Reset.
TEST(BandingReductionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildBandingReductionProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.sigmaValue, nullptr);
    ASSERT_NE(ui.protectFromHighlights, nullptr);
    ASSERT_NE(ui.verticalBanding, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Banding Reduction"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_FALSE(ui.protectFromHighlights->isChecked());
    EXPECT_FALSE(ui.verticalBanding->isChecked());
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    EXPECT_DOUBLE_EQ(ui.sigmaValue->value(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The CLAHE dialog opens on an eight-tile grid limited at two, at full amount, the preview on, and Apply, Close and
// Reset.
TEST(CLAHEProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildCLAHEProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.gridSizeValue, nullptr);
    ASSERT_NE(ui.contrastLimitValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("CLAHE"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_EQ(ui.gridSizeValue->value(), 8);
    EXPECT_DOUBLE_EQ(ui.contrastLimitValue->value(), 2.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The High Pass Filter dialog opens at a radius of ten, the preview on, and Apply, Close and Reset.
TEST(HighPassFilterProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildHighPassFilterProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.radiusValue, nullptr);
    ASSERT_NE(ui.radiusSlider, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("High Pass Filter"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_DOUBLE_EQ(ui.radiusValue->value(), 10.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The SCNR dialog opens at full amount keeping the lightness, its slider at the end of the thousand steps the window
// binds it to, with Apply and Close.
TEST(SCNRProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildSCNRProcessUi(&host);

    ASSERT_NE(ui.protectionType, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.amountSlider, nullptr);
    ASSERT_NE(ui.preserveLightness, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);

    EXPECT_EQ(host.size(), QSize(360, 190));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("SCNR"));
    EXPECT_TRUE(ui.preserveLightness->isChecked());
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    EXPECT_EQ(ui.amountSlider->minimum(), 0);
    EXPECT_EQ(ui.amountSlider->maximum(), 1000);
    EXPECT_EQ(ui.amountSlider->value(), ui.amountSlider->maximum());
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close});
}

// The Unsharp Mask dialog opens at a radius and an amount of one, the preview on, and Apply, Close and Reset.
TEST(UnsharpMaskProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildUnsharpMaskProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.radiusValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(300, 200));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Unsharp Mask"));
    EXPECT_TRUE(ui.preview->isChecked());
    EXPECT_DOUBLE_EQ(ui.radiusValue->value(), 1.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

} // anonymous namespace
