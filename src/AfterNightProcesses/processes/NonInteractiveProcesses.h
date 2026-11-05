// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image menu's actions that run at once on the active view, with no window of their own: duplicating the view,
/// extracting a sequence's frame, the thirty-nine channel and conversion actions, converting a sequence's container,
/// the mirrors and turns, and converting to grey, to colour and to the negative.
///
/// Each runs its engine work on a worker and publishes on the GUI thread, and says why when it publishes nothing: a
/// failure reaches the user and the log, and only a stop the user asked for is quiet. An action over a still image
/// reads the view's image under a lease; one over a sequence takes the view for the run, shows a progress window whose
/// Cancel stops it, reads every frame through the sequence's frame reader and writes a new sequence that is published
/// whole or not at all. An extraction and a conversion open new views beside the view; an in-place action becomes a
/// step of the view's history before the view shows it.
#pragma once

#include "AfterNightProcessesExport.h"

class ImageViewControl;

namespace process_utils
{
struct ChannelAction;
}

class AFTERNIGHTPROCESSES_API NonInteractiveProcesses
{
public:
    /// Opens a copy of the view's image, or the view's sequence opened again from its file, in a new view.
    static void duplicateImage(ImageViewControl* control);

    /// Opens the frame a sequence view shows as a still image in a new view.
    static void extractFrame(ImageViewControl* control);

    /// Runs a channel or conversion action over the view's image, or over every frame of its sequence, and opens what
    /// it makes in new views beside it.
    ///
    /// @param control the view.
    /// @param action the action's row of `process_utils::channelActions()`.
    static void runChannelAction(ImageViewControl* control, const process_utils::ChannelAction& action);

    static void extractRedChannel(ImageViewControl* control);
    static void extractGreenChannel(ImageViewControl* control);
    static void extractBlueChannel(ImageViewControl* control);
    static void extractRGBChannels(ImageViewControl* control);

    static void extractCIEL(ImageViewControl* control);
    static void extractCIEA(ImageViewControl* control);
    static void extractCIEB(ImageViewControl* control);
    static void extractCIEAB(ImageViewControl* control);
    static void extractCIELAB(ImageViewControl* control);

    static void extractCIEC(ImageViewControl* control);
    static void extractCIEH(ImageViewControl* control);
    static void extractCIELCH(ImageViewControl* control);

    static void extractCIEX(ImageViewControl* control);
    static void extractCIEY(ImageViewControl* control);
    static void extractCIEZ(ImageViewControl* control);
    static void extractCIEXYZ(ImageViewControl* control);

    static void extractHSL_H(ImageViewControl* control);
    static void extractHSL_S(ImageViewControl* control);
    static void extractHSL_L(ImageViewControl* control);
    static void extractHSL(ImageViewControl* control);

    static void extractHSV_H(ImageViewControl* control);
    static void extractHSV_S(ImageViewControl* control);
    static void extractHSV_V(ImageViewControl* control);
    static void extractHSV(ImageViewControl* control);

    static void extractHSI_H(ImageViewControl* control);
    static void extractHSI_S(ImageViewControl* control);
    static void extractHSI_I(ImageViewControl* control);
    static void extractHSI(ImageViewControl* control);

    static void extractYCbCr_Y(ImageViewControl* control);
    static void extractYCbCr_Cb(ImageViewControl* control);
    static void extractYCbCr_Cr(ImageViewControl* control);
    static void extractYCbCr(ImageViewControl* control);

    static void convertTo8Bit(ImageViewControl* control);
    static void convertToSigned16Bit(ImageViewControl* control);
    static void convertToUnsigned16Bit(ImageViewControl* control);
    static void convertToSigned32Bit(ImageViewControl* control);
    static void convertToUnsigned32Bit(ImageViewControl* control);
    static void convertToFloat32(ImageViewControl* control);
    static void convertToFloat64(ImageViewControl* control);

    /// Writes a sequence view's frames into a SER file, or an image list, and shows that sequence in the view.
    static void convertToSERSequence(ImageViewControl* control);
    static void convertToImageListSequence(ImageViewControl* control);

    static void horizontalMirror(ImageViewControl* control);
    static void verticalMirror(ImageViewControl* control);

    static void rotate180Degree(ImageViewControl* control);
    static void rotate90DegreeRight(ImageViewControl* control);
    static void rotate90DegreeLeft(ImageViewControl* control);

    static void convertToGrayscale(ImageViewControl* control);
    static void convertToRGB(ImageViewControl* control);
    static void negativeTransformation(ImageViewControl* control);
};
