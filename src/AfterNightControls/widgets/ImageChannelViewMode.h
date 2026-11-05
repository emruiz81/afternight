// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstdint>

enum class ImageChannelViewMode : unsigned char
{
    RGB_K_MODE = 0,
    RGB_R_MODE,
    RGB_G_MODE,
    RGB_B_MODE,
    CIE_AB_MODE,
    CIE_L_MODE,
    CIE_a_MODE,
    CIE_b_MODE,
    CIE_c_MODE,
    CIE_h_MODE,
    CIE_X_MODE,
    CIE_Y_MODE,
    CIE_Z_MODE,
    HSL_H_MODE,
    HSL_S_MODE,
    HSL_L_MODE,
    HSV_H_MODE,
    HSV_S_MODE,
    HSV_V_MODE,
    HSI_H_MODE,
    HSI_S_MODE,
    HSI_I_MODE,
    YCBCR_Y_MODE,
    YCBCR_CB_MODE,
    YCBCR_CR_MODE
};
