// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "app/ImageOperationsCoordinator.h"

#include <functional>

#include <QString>

class ImageViewControl;

namespace an::ui
{

/// The non-interactive processes' half of the image menu's operations on the active view: duplicating it, converting a
/// sequence to SER or to an image list, the mirrors and the quarter and half turns, the negative, and converting to
/// grey or to colour. The image operations coordinator runs them once it has found the view; defined with the
/// non-interactive processes' other entry points.
///
/// @return the operations, every one set.
[[nodiscard]] AFTERNIGHTPROCESSES_API ImageOperationsCoordinator::NonInteractiveOperations
nonInteractiveImageOperations();

inline bool executeSingleImageNonInteractiveOperation(
    ImageViewControl* activeControl, const QString& errorTitle, const QString& missingSelectionMessage,
    const std::function<void(ImageViewControl*)>& operation,
    const std::function<void(const QString& title, const QString& message)>& notifyError)
{
    if (!activeControl)
    {
        if (notifyError)
        {
            notifyError(errorTitle, missingSelectionMessage);
        }
        return false;
    }

    if (operation)
    {
        operation(activeControl);
    }

    return true;
}

class AFTERNIGHTPROCESSES_API ChannelExtractor
{
public:
    ChannelExtractor() = default;

    void extractRedChannel() const;
    void extractGreenChannel() const;
    void extractBlueChannel() const;
    void extractRGBChannels() const;

    void extractCIEL() const;
    void extractCIEA() const;
    void extractCIEB() const;
    void extractCIEAB() const;
    void extractCIELAB() const;

    void extractCIEC() const;
    void extractCIEH() const;
    void extractCIELCH() const;

    void extractCIEX() const;
    void extractCIEY() const;
    void extractCIEZ() const;
    void extractCIEXYZ() const;

    void extractHSL_H() const;
    void extractHSL_S() const;
    void extractHSL_L() const;
    void extractHSL() const;

    void extractHSV_H() const;
    void extractHSV_S() const;
    void extractHSV_V() const;
    void extractHSV() const;

    void extractHSI_H() const;
    void extractHSI_S() const;
    void extractHSI_I() const;
    void extractHSI() const;

    void extractYCbCr_Y() const;
    void extractYCbCr_Cb() const;
    void extractYCbCr_Cr() const;
    void extractYCbCr() const;

    void convertTo8Bit() const;
    void convertToSigned16Bit() const;
    void convertToUnsigned16Bit() const;
    void convertToSigned32Bit() const;
    void convertToUnsigned32Bit() const;
    void convertToFloat32() const;
    void convertToFloat64() const;

private:
    bool executeOperation(const QString& title, const QString& message,
                          const std::function<void(ImageViewControl*)>& operation) const;

    ImageViewControl* activeControl() const;
};

} // namespace an::ui
