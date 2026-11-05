// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The channel menu's choices as the engine's channel scopes: every mode a view can show maps to one scope or the
/// chrominance view and back, and a choice the image cannot show reads as the whole image.

#include <gtest/gtest.h>

#include "widgets/ChannelDisplayStrategy.h"

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

namespace
{

class ChannelDisplayLookupTest : public ::testing::TestWithParam<ImageChannelViewMode>
{
};

TEST_P(ChannelDisplayLookupTest, EveryModeMapsToAChannelAndBack)
{
    const an::ui::ViewChannel channel = an::ui::viewChannelForMode(GetParam());
    EXPECT_EQ(an::ui::channelViewModeFor(channel), GetParam());
}

INSTANTIATE_TEST_SUITE_P(AllModes, ChannelDisplayLookupTest,
                         ::testing::Values(ImageChannelViewMode::RGB_K_MODE, ImageChannelViewMode::RGB_R_MODE,
                                           ImageChannelViewMode::RGB_G_MODE, ImageChannelViewMode::RGB_B_MODE,
                                           ImageChannelViewMode::CIE_AB_MODE, ImageChannelViewMode::CIE_L_MODE,
                                           ImageChannelViewMode::CIE_a_MODE, ImageChannelViewMode::CIE_b_MODE,
                                           ImageChannelViewMode::CIE_c_MODE, ImageChannelViewMode::CIE_h_MODE,
                                           ImageChannelViewMode::CIE_X_MODE, ImageChannelViewMode::CIE_Y_MODE,
                                           ImageChannelViewMode::CIE_Z_MODE, ImageChannelViewMode::HSL_H_MODE,
                                           ImageChannelViewMode::HSL_S_MODE, ImageChannelViewMode::HSL_L_MODE,
                                           ImageChannelViewMode::HSV_H_MODE, ImageChannelViewMode::HSV_S_MODE,
                                           ImageChannelViewMode::HSV_V_MODE, ImageChannelViewMode::HSI_H_MODE,
                                           ImageChannelViewMode::HSI_S_MODE, ImageChannelViewMode::HSI_I_MODE,
                                           ImageChannelViewMode::YCBCR_Y_MODE, ImageChannelViewMode::YCBCR_CB_MODE,
                                           ImageChannelViewMode::YCBCR_CR_MODE));

} // namespace

TEST(ImageViewControlChannelSwitching, TheMenusCompositionsAreTheEnginesScopes)
{
    EXPECT_EQ(an::ui::viewChannelForComposition(ImageCompositionMode::rgb_mode), an::ui::ViewChannel{});
    EXPECT_EQ(an::ui::viewChannelForComposition(ImageCompositionMode::r_mode).scope, an::ChannelScope::Red);
    EXPECT_EQ(an::ui::viewChannelForComposition(ImageCompositionMode::ciel_mode).scope, an::ChannelScope::CieLightness);
    EXPECT_EQ(an::ui::viewChannelForComposition(ImageCompositionMode::hsi_i_mode).scope,
              an::ChannelScope::HsiIntensity);
    EXPECT_EQ(an::ui::viewChannelForComposition(ImageCompositionMode::ycbcr_cr_mode).scope,
              an::ChannelScope::YCbCrRedChroma);
    EXPECT_TRUE(an::ui::viewChannelForComposition(ImageCompositionMode::cieab_mode).chrominance);
    for (const ImageCompositionMode composite :
         {ImageCompositionMode::cielab_mode, ImageCompositionMode::cielch_mode, ImageCompositionMode::ciexyz_mode,
          ImageCompositionMode::hsl_mode, ImageCompositionMode::hsv_mode, ImageCompositionMode::hsi_mode,
          ImageCompositionMode::ycbcr_mode})
    {
        EXPECT_EQ(an::ui::viewChannelForComposition(composite), an::ui::ViewChannel{})
            << "a composite the menu never renders shows the whole image";
    }
}

TEST(ImageViewControlChannelSwitching, AGreyImageShowsItselfWhateverIsAsked)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4u, 4u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> grey =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    ASSERT_TRUE(grey.has_value());
    EXPECT_EQ(an::ui::effectiveViewChannel(*grey, an::ui::ViewChannel{.scope = an::ChannelScope::Red}),
              an::ui::ViewChannel{});
    EXPECT_EQ(an::ui::effectiveViewChannel(*grey, an::ui::ViewChannel{.chrominance = true}), an::ui::ViewChannel{});
}
