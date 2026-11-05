// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask menu's modes as the engine's overlay requests: six tints in the product's colours, the mask itself, and
/// the shown image multiplied by the mask, each inverted or not and for the channel the view shows.

#include <gtest/gtest.h>

#include "widgets/MaskRenderer.h"

TEST(ImageViewControlMaskOperations, EachTintModeLaysDownItsOwnOpaqueColour)
{
    struct Expected
    {
        MaskRenderingMode mode;
        std::uint8_t red;
        std::uint8_t green;
        std::uint8_t blue;
    };
    for (const Expected& expected :
         {Expected{MaskRenderingMode::red_mode, 255u, 0u, 0u}, Expected{MaskRenderingMode::green_mode, 0u, 255u, 0u},
          Expected{MaskRenderingMode::blue_mode, 0u, 0u, 255u},
          Expected{MaskRenderingMode::yellow_mode, 255u, 255u, 0u},
          Expected{MaskRenderingMode::magenta_mode, 255u, 0u, 255u},
          Expected{MaskRenderingMode::cyan_mode, 0u, 255u, 255u}})
    {
        const an::MaskOverlayColor tint = an::ui::maskTintFor(expected.mode);
        EXPECT_EQ(tint.red, expected.red);
        EXPECT_EQ(tint.green, expected.green);
        EXPECT_EQ(tint.blue, expected.blue);
        EXPECT_EQ(tint.alpha, 255u);
        EXPECT_EQ(an::ui::maskOverlaySettingsFor(expected.mode, false, {}).mode, an::MaskOverlayMode::Tint);
    }
}

TEST(ImageViewControlMaskOperations, ReplaceAndMultiplyAreTheirOwnOverlays)
{
    EXPECT_EQ(an::ui::maskOverlaySettingsFor(MaskRenderingMode::replace_mode, false, {}).mode,
              an::MaskOverlayMode::Replace);
    EXPECT_EQ(an::ui::maskOverlaySettingsFor(MaskRenderingMode::multiply_mode, false, {}).mode,
              an::MaskOverlayMode::Multiply);
}

TEST(ImageViewControlMaskOperations, TheOverlayCarriesTheInversionAndTheShownChannel)
{
    const an::ui::ViewChannel green{.scope = an::ChannelScope::Green};
    const an::ui::ViewMaskOverlaySettings settings =
        an::ui::maskOverlaySettingsFor(MaskRenderingMode::cyan_mode, true, green);
    EXPECT_TRUE(settings.invert);
    EXPECT_EQ(settings.shownChannel, green);
    EXPECT_FALSE(an::ui::maskOverlaySettingsFor(MaskRenderingMode::cyan_mode, false, green).invert);
}
