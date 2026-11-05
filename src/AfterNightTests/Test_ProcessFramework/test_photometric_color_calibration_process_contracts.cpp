// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Photometric Color Calibration dialog's background controls as its builder makes them: the sky is measured over
/// the whole image or over a region a person draws or types, and the dialog offers no manual background limits.

#include "processes/PhotometricColorCalibration/PhotometricColorCalibrationUiBuilder.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include <gtest/gtest.h>

namespace
{

// The background is neutralized over the whole image unless a region overrides it: the dialog offers a checkable
// "Background ROI Override" region, unchecked, with a button that draws it and four spin boxes that type it, and no
// spin box of its own beside the magnitude limit and the match radius - the sky's limits are the engine's.
TEST(PhotometricColorCalibrationProcessContracts, TheSkyIsMeasuredOverTheImageOrARegionAndNeverBetweenTypedLimits)
{
    QWidget host;
    const auto ui = an::ui::processes::buildPhotometricColorCalibrationProcessUi(&host);

    ASSERT_NE(ui.backgroundNeutralizationCheck, nullptr);
    EXPECT_TRUE(ui.backgroundNeutralizationCheck->isChecked());
    ASSERT_NE(ui.backgroundRoiGroupBox, nullptr);
    EXPECT_EQ(ui.backgroundRoiGroupBox->title(), QStringLiteral("Background ROI Override"));
    EXPECT_TRUE(ui.backgroundRoiGroupBox->isCheckable());
    EXPECT_FALSE(ui.backgroundRoiGroupBox->isChecked());
    ASSERT_NE(ui.backgroundRoiSelectionButton, nullptr);
    EXPECT_TRUE(ui.backgroundRoiSelectionButton->isCheckable());
    for (const QSpinBox* edge :
         {ui.backgroundRoiLeftValue, ui.backgroundRoiTopValue, ui.backgroundRoiWidthValue, ui.backgroundRoiHeightValue})
    {
        ASSERT_NE(edge, nullptr);
        EXPECT_EQ(edge->parentWidget(), ui.backgroundRoiGroupBox) << edge->objectName().toStdString();
    }

    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("backgroundLowerLimitSpin")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("backgroundUpperLimitSpin")), nullptr);
    const QList<QDoubleSpinBox*> reals = host.findChildren<QDoubleSpinBox*>();
    ASSERT_EQ(reals.size(), 2);
    EXPECT_TRUE(reals.contains(ui.magnitudeLimitSpin));
    EXPECT_TRUE(reals.contains(ui.matchRadiusSpin));
}

} // namespace
