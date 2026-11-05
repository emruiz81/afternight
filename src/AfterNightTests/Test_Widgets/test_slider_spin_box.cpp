// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QSlider>
#include <QSpinBox>

#include "controls/QSliderSpinBox.h"

#include <vector>

// A control made over a window's spin box and slider belongs to the parent it is given, which deletes it.
TEST(WidgetsQSliderSpinBox, AControlBelongsToTheParentItIsGiven)
{
    QObject owner;
    QSpinBox spin;
    QSlider slider;

    const auto* over = new QSliderSpinBox(&spin, &slider, &owner);
    const auto* owned = new QSliderSpinBox(0, 10, 1, &owner);

    EXPECT_EQ(over->parent(), &owner);
    EXPECT_EQ(owned->parent(), &owner);
}

TEST(WidgetsQSliderDoubleSpinBox, AValueMovesTheSliderInProportionToItsRange)
{
    QSliderDoubleSpinBox control(0.0, 10.0, 0.1);

    control.setValue(2.5);

    EXPECT_EQ(control.getSlider()->value(), 250);
}

TEST(WidgetsQSliderDoubleSpinBox, ARangeWithNoWidthPutsTheSliderAtItsStart)
{
    // The star filter's ranges collapse to one value when every star shares it.
    QSliderDoubleSpinBox control(2.5, 2.5, 0.1);

    control.setValue(2.5);

    EXPECT_EQ(control.getSlider()->value(), 0);
}

// Pressing the slider and letting it go are told as an interaction - what a real-time preview window tells a drag by -
// for a control over a window's widgets and for one that makes its own.
TEST(WidgetsQSliderSpinBox, PressingTheSliderAndLettingItGoAreToldAsAnInteraction)
{
    QSpinBox spin;
    QSlider slider;
    QSliderSpinBox over(&spin, &slider);
    std::vector<bool> told;
    QObject::connect(&over, &QSliderSpinBox::interaction, [&told](bool held) { told.push_back(held); });

    slider.setSliderDown(true);
    slider.setValue(3);
    EXPECT_EQ(told, std::vector<bool>{true});
    slider.setSliderDown(false);
    EXPECT_EQ(told, (std::vector<bool>{true, false}));
    spin.setValue(5);
    EXPECT_EQ(told.size(), 2u) << "a value typed is no drag";

    QSliderDoubleSpinBox owned(0.0, 1.0, 0.1);
    std::vector<bool> toldOwned;
    QObject::connect(&owned, &QSliderDoubleSpinBox::interaction,
                     [&toldOwned](bool held) { toldOwned.push_back(held); });
    owned.getSlider()->setSliderDown(true);
    owned.getSlider()->setSliderDown(false);
    EXPECT_EQ(toldOwned, (std::vector<bool>{true, false}));
}

// A control handed to an owner that is no real-time preview window - a settings dialog's, a player's - is left as it
// is, and so is one handed to no owner.
TEST(WidgetsQSliderSpinBox, AControlHandedToAnOwnerThatShowsNoPreviewIsLeftAsItIs)
{
    QObject owner;
    QSliderSpinBox control(0, 10, 1);
    QSliderDoubleSpinBox realControl(0.0, 1.0, 0.1);
    an::ui::watchPreviewInteraction(&owner, &control);
    an::ui::watchPreviewInteraction(nullptr, &control);
    an::ui::watchPreviewInteraction(&owner, &realControl);
    an::ui::watchPreviewInteraction(nullptr, &realControl);

    std::vector<bool> told;
    QObject::connect(&control, &QSliderSpinBox::interaction, [&told](bool held) { told.push_back(held); });
    control.getSlider()->setSliderDown(true);
    control.getSlider()->setSliderDown(false);
    EXPECT_EQ(told, (std::vector<bool>{true, false}));
}
