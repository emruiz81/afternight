// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Compare dialog as its builder makes it: the widgets the window reads, laid out with the theme's spacing,
/// and Close.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/ImageCompareProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

namespace
{

// The dialog opens at its size with its histogram area and its toggles, laid out with the theme's spacing, and offers
// Close.
TEST(ImageCompareProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    auto ui = an::ui::processes::buildImageCompareProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.zoomIn, nullptr);
    ASSERT_NE(ui.zoomOut, nullptr);
    ASSERT_NE(ui.zoomReset, nullptr);
    ASSERT_NE(ui.gridButton, nullptr);
    ASSERT_NE(ui.showClippingInfo, nullptr);
    ASSERT_NE(ui.localChannelNormalization, nullptr);
    ASSERT_NE(ui.showHistogramAreas, nullptr);
    ASSERT_NE(ui.plotResolution, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.secondImageSelector, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Compare"));
    EXPECT_EQ(host.size(), QSize(1000, 627));
    EXPECT_EQ(host.minimumSize(), QSize(300, 200));

    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins(),
              QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    EXPECT_EQ(ui.horizontalLayoutMain->stretch(0), 0);
    EXPECT_EQ(ui.horizontalLayoutMain->stretch(1), -1);

    EXPECT_EQ(ui.chartScrollArea->minimumSize(), QSize(380, 220));
    EXPECT_TRUE(ui.chartScrollArea->widgetResizable());

    EXPECT_TRUE(ui.gridButton->isCheckable());
    EXPECT_TRUE(ui.gridButton->isChecked());
    EXPECT_TRUE(ui.showClippingInfo->isCheckable());
    EXPECT_TRUE(ui.showClippingInfo->isChecked());
    EXPECT_TRUE(ui.localChannelNormalization->isCheckable());
    EXPECT_FALSE(ui.localChannelNormalization->isChecked());
    EXPECT_TRUE(ui.showHistogramAreas->isCheckable());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
}

} // anonymous namespace
