// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ChannelExtractor.h"

#include "processes/NonInteractiveProcesses.h"
#include "processes/ProcessCommonUtils.h"

namespace an::ui
{

bool ChannelExtractor::executeOperation(const QString& title, const QString& message,
                                        const std::function<void(ImageViewControl*)>& operation) const
{
    return executeSingleImageNonInteractiveOperation(
        activeControl(), title, message, operation, [](const QString& errorTitle, const QString& errorMessage) {
            process_utils::notifyErrorMessage(process_utils::currentMainWindow(), errorTitle, errorMessage);
        });
}

ImageViewControl* ChannelExtractor::activeControl() const
{
    return process_utils::currentActiveViewControl();
}

void ChannelExtractor::extractRedChannel() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractRedChannel(control); });
}

void ChannelExtractor::extractGreenChannel() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractGreenChannel(control); });
}

void ChannelExtractor::extractBlueChannel() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractBlueChannel(control); });
}

void ChannelExtractor::extractRGBChannels() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractRGBChannels(control); });
}

void ChannelExtractor::extractCIEL() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEL(control); });
}

void ChannelExtractor::extractCIEA() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEA(control); });
}

void ChannelExtractor::extractCIEB() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEB(control); });
}

void ChannelExtractor::extractCIEAB() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEAB(control); });
}

void ChannelExtractor::extractCIELAB() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIELAB(control); });
}

void ChannelExtractor::extractCIEC() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEC(control); });
}

void ChannelExtractor::extractCIEH() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEH(control); });
}

void ChannelExtractor::extractCIELCH() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIELCH(control); });
}

void ChannelExtractor::extractCIEX() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEX(control); });
}

void ChannelExtractor::extractCIEY() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEY(control); });
}

void ChannelExtractor::extractCIEZ() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEZ(control); });
}

void ChannelExtractor::extractCIEXYZ() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractCIEXYZ(control); });
}

void ChannelExtractor::extractHSL_H() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSL_H(control); });
}

void ChannelExtractor::extractHSL_S() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSL_S(control); });
}

void ChannelExtractor::extractHSL_L() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSL_L(control); });
}

void ChannelExtractor::extractHSL() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSL(control); });
}

void ChannelExtractor::extractHSV_H() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSV_H(control); });
}

void ChannelExtractor::extractHSV_S() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSV_S(control); });
}

void ChannelExtractor::extractHSV_V() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSV_V(control); });
}

void ChannelExtractor::extractHSV() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSV(control); });
}

void ChannelExtractor::extractHSI_H() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSI_H(control); });
}

void ChannelExtractor::extractHSI_S() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSI_S(control); });
}

void ChannelExtractor::extractHSI_I() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSI_I(control); });
}

void ChannelExtractor::extractHSI() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractHSI(control); });
}

void ChannelExtractor::extractYCbCr_Y() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractYCbCr_Y(control); });
}

void ChannelExtractor::extractYCbCr_Cb() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractYCbCr_Cb(control); });
}

void ChannelExtractor::extractYCbCr_Cr() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channel from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractYCbCr_Cr(control); });
}

void ChannelExtractor::extractYCbCr() const
{
    executeOperation(QStringLiteral("Extract channel"),
                     QStringLiteral("No selected image or sequence to extract the channels from"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::extractYCbCr(control); });
}

void ChannelExtractor::convertTo8Bit() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertTo8Bit(control); });
}

void ChannelExtractor::convertToSigned16Bit() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToSigned16Bit(control); });
}

void ChannelExtractor::convertToUnsigned16Bit() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToUnsigned16Bit(control); });
}

void ChannelExtractor::convertToSigned32Bit() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToSigned32Bit(control); });
}

void ChannelExtractor::convertToUnsigned32Bit() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToUnsigned32Bit(control); });
}

void ChannelExtractor::convertToFloat32() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToFloat32(control); });
}

void ChannelExtractor::convertToFloat64() const
{
    executeOperation(QStringLiteral("Convert format"), QStringLiteral("No selected image or sequence to convert"),
                     [](ImageViewControl* control) { NonInteractiveProcesses::convertToFloat64(control); });
}

ImageOperationsCoordinator::NonInteractiveOperations nonInteractiveImageOperations()
{
    ImageOperationsCoordinator::NonInteractiveOperations operations;
    operations.duplicateImage = [](ImageViewControl* control) { NonInteractiveProcesses::duplicateImage(control); };
    operations.convertToSERSequence = [](ImageViewControl* control) {
        NonInteractiveProcesses::convertToSERSequence(control);
    };
    operations.convertToImageListSequence = [](ImageViewControl* control) {
        NonInteractiveProcesses::convertToImageListSequence(control);
    };
    operations.horizontalMirror = [](ImageViewControl* control) { NonInteractiveProcesses::horizontalMirror(control); };
    operations.verticalMirror = [](ImageViewControl* control) { NonInteractiveProcesses::verticalMirror(control); };
    operations.rotate180Degree = [](ImageViewControl* control) { NonInteractiveProcesses::rotate180Degree(control); };
    operations.rotate90DegreeRight = [](ImageViewControl* control) {
        NonInteractiveProcesses::rotate90DegreeRight(control);
    };
    operations.rotate90DegreeLeft = [](ImageViewControl* control) {
        NonInteractiveProcesses::rotate90DegreeLeft(control);
    };
    operations.negativeTransformation = [](ImageViewControl* control) {
        NonInteractiveProcesses::negativeTransformation(control);
    };
    operations.convertToGrayscale = [](ImageViewControl* control) {
        NonInteractiveProcesses::convertToGrayscale(control);
    };
    operations.convertToRGB = [](ImageViewControl* control) { NonInteractiveProcesses::convertToRGB(control); };
    return operations;
}

} // namespace an::ui
