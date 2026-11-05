// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a view shows its mask: the modes the mask menu offers, the six tint colours, and the overlay request each mode
/// is.
///
/// The engine writes the overlay as one layer - a tint whose opacity is one minus the mask, the mask itself, or what
/// the view shows multiplied by the mask - and the colours stay here, because a colour a menu offers is the product's
/// choice.
#pragma once

#include "widgets/ViewPresentation.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>

/// What the mask menu offers: a tint in one of six colours, the mask itself, or the image multiplied by the mask.
enum class MaskRenderingMode : unsigned char
{
    red_mode,
    green_mode,
    blue_mode,
    yellow_mode,
    magenta_mode,
    cyan_mode,
    replace_mode,
    multiply_mode
};

namespace an::ui
{

/// The colour a tint mode lays down; opaque black for a mode that is not a tint.
///
/// @param mode the mode.
/// @return the colour.
[[nodiscard]] constexpr an::MaskOverlayColor maskTintFor(MaskRenderingMode mode) noexcept
{
    switch (mode)
    {
    case MaskRenderingMode::red_mode:
        return {.red = 255u, .green = 0u, .blue = 0u, .alpha = 255u};
    case MaskRenderingMode::green_mode:
        return {.red = 0u, .green = 255u, .blue = 0u, .alpha = 255u};
    case MaskRenderingMode::blue_mode:
        return {.red = 0u, .green = 0u, .blue = 255u, .alpha = 255u};
    case MaskRenderingMode::yellow_mode:
        return {.red = 255u, .green = 255u, .blue = 0u, .alpha = 255u};
    case MaskRenderingMode::magenta_mode:
        return {.red = 255u, .green = 0u, .blue = 255u, .alpha = 255u};
    case MaskRenderingMode::cyan_mode:
        return {.red = 0u, .green = 255u, .blue = 255u, .alpha = 255u};
    case MaskRenderingMode::replace_mode:
    case MaskRenderingMode::multiply_mode:
        break;
    }
    return {.red = 0u, .green = 0u, .blue = 0u, .alpha = 255u};
}

/// The overlay a mode asks the engine for.
///
/// @param mode the mode.
/// @param invert whether the mask is shown inverted.
/// @param shownChannel what the view shows of its image.
/// @return the overlay's settings.
[[nodiscard]] constexpr ViewMaskOverlaySettings maskOverlaySettingsFor(MaskRenderingMode mode, bool invert,
                                                                       ViewChannel shownChannel) noexcept
{
    ViewMaskOverlaySettings settings;
    switch (mode)
    {
    case MaskRenderingMode::replace_mode:
        settings.mode = an::MaskOverlayMode::Replace;
        break;
    case MaskRenderingMode::multiply_mode:
        settings.mode = an::MaskOverlayMode::Multiply;
        break;
    default:
        settings.mode = an::MaskOverlayMode::Tint;
        break;
    }
    settings.tint = maskTintFor(mode);
    settings.invert = invert;
    settings.shownChannel = shownChannel;
    return settings;
}

} // namespace an::ui
