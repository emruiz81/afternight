// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star dialogs as their builders make them - Star Mask, Multiscale Sharpen Alpha, Starmask Recombination, Profiled
/// Starmask Recombination and the two star profilers: the widgets each window reads, their titles and their buttons.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QWidget>

#include "processes/ProfiledStarmaskRecombinationProcessUiBuilder.h"
#include "processes/SharpenAlphaProcessUiBuilder.h"
#include "processes/StarMaskProcessUiBuilder.h"
#include "processes/StarProfilerProcessV1UiBuilder.h"
#include "processes/StarProfilerProcessV2UiBuilder.h"
#include "processes/StarmaskRecombinationProcessUiBuilder.h"

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

// The Star Mask dialog opens with its star shape, growth and size limits; the engine's detector decides which stars
// there are, so the dialog offers no detection threshold and no roundness tolerance, and a star grows by at least a
// hundredth of its radius.
TEST(StarMaskProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarMaskProcessUi(&host);

    ASSERT_NE(ui.starShapeSelector, nullptr);
    ASSERT_NE(ui.percentIncreaseValue, nullptr);
    ASSERT_NE(ui.minSizeValue, nullptr);
    ASSERT_NE(ui.maxSizeValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Star Mask"));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("detectionThresholdValue")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("roundnessToleranceValue")), nullptr);
    EXPECT_DOUBLE_EQ(ui.percentIncreaseValue->minimum(), 0.01);
    EXPECT_DOUBLE_EQ(ui.percentIncreaseValue->maximum(), 1.0);
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Multiscale Sharpen Alpha dialog opens with the widgets its window reads, and Apply, Close and Reset.
TEST(SharpenAlphaProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildSharpenAlphaProcessUi(&host);

    ASSERT_NE(ui.amplitudeValue, nullptr);
    ASSERT_NE(ui.numOfScalesValue, nullptr);
    ASSERT_NE(ui.anisotropyValue, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Multiscale Sharpen Alpha"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Apply, QDialogButtonBox::Close, QDialogButtonBox::Reset});
}

// The Starmask Recombination dialog opens with the widgets its window reads and its buttons.
TEST(StarmaskRecombinationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarmaskRecombinationProcessUi(&host);

    ASSERT_NE(ui.starmaskImage, nullptr);
    ASSERT_NE(ui.starlessImage, nullptr);
    ASSERT_NE(ui.maskImage, nullptr);
    ASSERT_NE(ui.invertMask, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Starmask Recombination"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close, QDialogButtonBox::Ok});
}

// The Profiled Starmask Recombination dialog opens with the widgets its window reads and its buttons.
TEST(ProfiledStarmaskRecombinationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildProfiledStarmaskRecombinationProcessUi(&host);

    ASSERT_NE(ui.starmaskImage, nullptr);
    ASSERT_NE(ui.starlessImage, nullptr);
    ASSERT_NE(ui.starsProfile, nullptr);
    ASSERT_NE(ui.shapeSelector, nullptr);
    ASSERT_NE(ui.scaleFactorValue, nullptr);
    ASSERT_NE(ui.preview, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Profiled Starmask Recombination"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close, QDialogButtonBox::Ok, QDialogButtonBox::Reset});
}

// The first star profiler's dialog opens with the widgets its window reads and its buttons.
TEST(StarProfilerProcessV1ProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarProfilerProcessV1Ui(&host);

    ASSERT_NE(ui.detectStarsButton, nullptr);
    ASSERT_NE(ui.resetButton, nullptr);
    ASSERT_NE(ui.loadProfileButton, nullptr);
    ASSERT_NE(ui.saveProfileButton, nullptr);
    ASSERT_NE(ui.starsFilterButton, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Star Profiler V1"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close});
}

// The star profiler's dialog opens with the widgets its window reads and its buttons.
TEST(StarProfilerProcessV2ProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildStarProfilerProcessV2Ui(&host);

    ASSERT_NE(ui.detectStarsButton, nullptr);
    ASSERT_NE(ui.radiusValue, nullptr);
    ASSERT_NE(ui.thresholdValue, nullptr);
    ASSERT_NE(ui.profileTypeSelector, nullptr);
    ASSERT_NE(ui.relaxPSFChecks, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Star Profiler"));
    expectButtonSet(ui.buttonBox, {QDialogButtonBox::Close});
}

} // anonymous namespace
