// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The one table between the channel choices the viewer offers and what the engine shows.
///
/// The viewer offers twenty-five channel views. The engine has one closed enumeration of the planes it extracts,
/// `ChannelScope`, and the viewer's chrominance view composes two of them. What remains here is the product's side: the
/// composition mode the toolbar and the menus name, the channel view mode a view reports, and the one mapping of each
/// onto `ViewChannel`.
#pragma once

#include "widgets/ImageChannelViewMode.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>

#include <cstdint>

/// What the viewer's channel menu and toolbar name. The composite values - the whole of an encoding - are not shown on
/// their own; they name a family of choices.
enum class ImageCompositionMode : unsigned char
{
    rgb_mode,
    r_mode,
    g_mode,
    b_mode,
    ciel_mode,
    ciea_mode,
    cieb_mode,
    ciec_mode,
    cieh_mode,
    cieab_mode,
    cielab_mode, // Not used for rendering
    cielch_mode, // Not used for rendering
    ciex_mode,
    ciey_mode,
    ciez_mode,
    ciexyz_mode, // Not used for rendering
    hsl_h_mode,
    hsl_s_mode,
    hsl_l_mode,
    hsl_mode, // Not used for rendering
    hsv_h_mode,
    hsv_s_mode,
    hsv_v_mode,
    hsv_mode, // Not used for rendering
    hsi_h_mode,
    hsi_s_mode,
    hsi_i_mode,
    hsi_mode, // Not used for rendering
    ycbcr_y_mode,
    ycbcr_cb_mode,
    ycbcr_cr_mode,
    ycbcr_mode // Not used for rendering
};

namespace an::ui
{

/// What a composition mode shows: one channel scope, the chrominance view, or the whole image for a composite mode
/// that names no single plane.
///
/// @param mode the mode.
/// @return the channel.
[[nodiscard]] constexpr ViewChannel viewChannelForComposition(ImageCompositionMode mode) noexcept
{
    switch (mode)
    {
    case ImageCompositionMode::r_mode:
        return {.scope = an::ChannelScope::Red};
    case ImageCompositionMode::g_mode:
        return {.scope = an::ChannelScope::Green};
    case ImageCompositionMode::b_mode:
        return {.scope = an::ChannelScope::Blue};
    case ImageCompositionMode::ciel_mode:
        return {.scope = an::ChannelScope::CieLightness};
    case ImageCompositionMode::ciea_mode:
        return {.scope = an::ChannelScope::CieA};
    case ImageCompositionMode::cieb_mode:
        return {.scope = an::ChannelScope::CieB};
    case ImageCompositionMode::ciec_mode:
        return {.scope = an::ChannelScope::CieChroma};
    case ImageCompositionMode::cieh_mode:
        return {.scope = an::ChannelScope::CieHue};
    case ImageCompositionMode::cieab_mode:
        return {.scope = an::ChannelScope::Whole, .chrominance = true};
    case ImageCompositionMode::ciex_mode:
        return {.scope = an::ChannelScope::CieX};
    case ImageCompositionMode::ciey_mode:
        return {.scope = an::ChannelScope::CieY};
    case ImageCompositionMode::ciez_mode:
        return {.scope = an::ChannelScope::CieZ};
    case ImageCompositionMode::hsl_h_mode:
        return {.scope = an::ChannelScope::HslHue};
    case ImageCompositionMode::hsl_s_mode:
        return {.scope = an::ChannelScope::HslSaturation};
    case ImageCompositionMode::hsl_l_mode:
        return {.scope = an::ChannelScope::HslLightness};
    case ImageCompositionMode::hsv_h_mode:
        return {.scope = an::ChannelScope::HsvHue};
    case ImageCompositionMode::hsv_s_mode:
        return {.scope = an::ChannelScope::HsvSaturation};
    case ImageCompositionMode::hsv_v_mode:
        return {.scope = an::ChannelScope::HsvValue};
    case ImageCompositionMode::hsi_h_mode:
        return {.scope = an::ChannelScope::HsiHue};
    case ImageCompositionMode::hsi_s_mode:
        return {.scope = an::ChannelScope::HsiSaturation};
    case ImageCompositionMode::hsi_i_mode:
        return {.scope = an::ChannelScope::HsiIntensity};
    case ImageCompositionMode::ycbcr_y_mode:
        return {.scope = an::ChannelScope::YCbCrLuma};
    case ImageCompositionMode::ycbcr_cb_mode:
        return {.scope = an::ChannelScope::YCbCrBlueChroma};
    case ImageCompositionMode::ycbcr_cr_mode:
        return {.scope = an::ChannelScope::YCbCrRedChroma};
    case ImageCompositionMode::rgb_mode:
    case ImageCompositionMode::cielab_mode:
    case ImageCompositionMode::cielch_mode:
    case ImageCompositionMode::ciexyz_mode:
    case ImageCompositionMode::hsl_mode:
    case ImageCompositionMode::hsv_mode:
    case ImageCompositionMode::hsi_mode:
    case ImageCompositionMode::ycbcr_mode:
        break;
    }
    return {};
}

/// The channel view mode a view reports for what it shows.
///
/// @param channel what the view shows.
/// @return the mode.
[[nodiscard]] constexpr ImageChannelViewMode channelViewModeFor(ViewChannel channel) noexcept
{
    if (channel.chrominance)
    {
        return ImageChannelViewMode::CIE_AB_MODE;
    }
    switch (channel.scope)
    {
    case an::ChannelScope::Red:
        return ImageChannelViewMode::RGB_R_MODE;
    case an::ChannelScope::Green:
        return ImageChannelViewMode::RGB_G_MODE;
    case an::ChannelScope::Blue:
        return ImageChannelViewMode::RGB_B_MODE;
    case an::ChannelScope::CieLightness:
        return ImageChannelViewMode::CIE_L_MODE;
    case an::ChannelScope::CieA:
        return ImageChannelViewMode::CIE_a_MODE;
    case an::ChannelScope::CieB:
        return ImageChannelViewMode::CIE_b_MODE;
    case an::ChannelScope::CieChroma:
        return ImageChannelViewMode::CIE_c_MODE;
    case an::ChannelScope::CieHue:
        return ImageChannelViewMode::CIE_h_MODE;
    case an::ChannelScope::CieX:
        return ImageChannelViewMode::CIE_X_MODE;
    case an::ChannelScope::CieY:
        return ImageChannelViewMode::CIE_Y_MODE;
    case an::ChannelScope::CieZ:
        return ImageChannelViewMode::CIE_Z_MODE;
    case an::ChannelScope::HslHue:
        return ImageChannelViewMode::HSL_H_MODE;
    case an::ChannelScope::HslSaturation:
        return ImageChannelViewMode::HSL_S_MODE;
    case an::ChannelScope::HslLightness:
        return ImageChannelViewMode::HSL_L_MODE;
    case an::ChannelScope::HsvHue:
        return ImageChannelViewMode::HSV_H_MODE;
    case an::ChannelScope::HsvSaturation:
        return ImageChannelViewMode::HSV_S_MODE;
    case an::ChannelScope::HsvValue:
        return ImageChannelViewMode::HSV_V_MODE;
    case an::ChannelScope::HsiHue:
        return ImageChannelViewMode::HSI_H_MODE;
    case an::ChannelScope::HsiSaturation:
        return ImageChannelViewMode::HSI_S_MODE;
    case an::ChannelScope::HsiIntensity:
        return ImageChannelViewMode::HSI_I_MODE;
    case an::ChannelScope::YCbCrLuma:
        return ImageChannelViewMode::YCBCR_Y_MODE;
    case an::ChannelScope::YCbCrBlueChroma:
        return ImageChannelViewMode::YCBCR_CB_MODE;
    case an::ChannelScope::YCbCrRedChroma:
        return ImageChannelViewMode::YCBCR_CR_MODE;
    case an::ChannelScope::Whole:
        break;
    }
    return ImageChannelViewMode::RGB_K_MODE;
}

/// What a channel view mode shows; the inverse of `channelViewModeFor`.
///
/// @param mode the mode.
/// @return the channel.
[[nodiscard]] constexpr ViewChannel viewChannelForMode(ImageChannelViewMode mode) noexcept
{
    switch (mode)
    {
    case ImageChannelViewMode::RGB_R_MODE:
        return {.scope = an::ChannelScope::Red};
    case ImageChannelViewMode::RGB_G_MODE:
        return {.scope = an::ChannelScope::Green};
    case ImageChannelViewMode::RGB_B_MODE:
        return {.scope = an::ChannelScope::Blue};
    case ImageChannelViewMode::CIE_AB_MODE:
        return {.scope = an::ChannelScope::Whole, .chrominance = true};
    case ImageChannelViewMode::CIE_L_MODE:
        return {.scope = an::ChannelScope::CieLightness};
    case ImageChannelViewMode::CIE_a_MODE:
        return {.scope = an::ChannelScope::CieA};
    case ImageChannelViewMode::CIE_b_MODE:
        return {.scope = an::ChannelScope::CieB};
    case ImageChannelViewMode::CIE_c_MODE:
        return {.scope = an::ChannelScope::CieChroma};
    case ImageChannelViewMode::CIE_h_MODE:
        return {.scope = an::ChannelScope::CieHue};
    case ImageChannelViewMode::CIE_X_MODE:
        return {.scope = an::ChannelScope::CieX};
    case ImageChannelViewMode::CIE_Y_MODE:
        return {.scope = an::ChannelScope::CieY};
    case ImageChannelViewMode::CIE_Z_MODE:
        return {.scope = an::ChannelScope::CieZ};
    case ImageChannelViewMode::HSL_H_MODE:
        return {.scope = an::ChannelScope::HslHue};
    case ImageChannelViewMode::HSL_S_MODE:
        return {.scope = an::ChannelScope::HslSaturation};
    case ImageChannelViewMode::HSL_L_MODE:
        return {.scope = an::ChannelScope::HslLightness};
    case ImageChannelViewMode::HSV_H_MODE:
        return {.scope = an::ChannelScope::HsvHue};
    case ImageChannelViewMode::HSV_S_MODE:
        return {.scope = an::ChannelScope::HsvSaturation};
    case ImageChannelViewMode::HSV_V_MODE:
        return {.scope = an::ChannelScope::HsvValue};
    case ImageChannelViewMode::HSI_H_MODE:
        return {.scope = an::ChannelScope::HsiHue};
    case ImageChannelViewMode::HSI_S_MODE:
        return {.scope = an::ChannelScope::HsiSaturation};
    case ImageChannelViewMode::HSI_I_MODE:
        return {.scope = an::ChannelScope::HsiIntensity};
    case ImageChannelViewMode::YCBCR_Y_MODE:
        return {.scope = an::ChannelScope::YCbCrLuma};
    case ImageChannelViewMode::YCBCR_CB_MODE:
        return {.scope = an::ChannelScope::YCbCrBlueChroma};
    case ImageChannelViewMode::YCBCR_CR_MODE:
        return {.scope = an::ChannelScope::YCbCrRedChroma};
    case ImageChannelViewMode::RGB_K_MODE:
        break;
    }
    return {};
}

} // namespace an::ui
