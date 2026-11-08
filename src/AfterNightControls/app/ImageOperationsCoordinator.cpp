// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ImageOperationsCoordinator.h"

#include "app/ShowInFileBrowser.h"
#include "controls/ImageRenameControl.h"
#include "controls/ImageViewControl.h"

namespace an::ui
{

ImageOperationsCoordinator::ImageOperationsCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies)
{
}

void ImageOperationsCoordinator::closeFile() const
{
    auto* control = activeControl();
    if (!control)
    {
        return;
    }

    if (!control->checkPendingChangesAndAskUserBeforeClosing() ||
        !control->checkProcessAttachedAndAskUserBeforeClosing())
    {
        return;
    }

    auto* focusedDock = control->getDockWidget();
    if (focusedDock)
    {
        control->deepClose();
    }
}

void ImageOperationsCoordinator::closeAllFiles() const
{
    auto controls = listViewControls();
    for (auto* control : controls)
    {
        if (!control)
        {
            continue;
        }

        if (!control->checkPendingChangesAndAskUserBeforeClosing() ||
            !control->checkProcessAttachedAndAskUserBeforeClosing())
        {
            continue;
        }

        control->deepClose();
    }
}

void ImageOperationsCoordinator::closeAllOtherFiles() const
{
    auto controls = listViewControls();
    if (controls.size() <= 1u)
    {
        return;
    }

    auto* active = activeControl();
    for (auto* control : controls)
    {
        if (!control || shouldSkipCloseCandidateWhenClosingOthers(control == active))
        {
            continue;
        }

        if (!control->checkPendingChangesAndAskUserBeforeClosing() ||
            !control->checkProcessAttachedAndAskUserBeforeClosing())
        {
            continue;
        }

        control->deepClose();
    }
}

void ImageOperationsCoordinator::duplicateImage() const
{
    auto* control = activeControlOrNotify("Image duplicate error", "No selected image to duplicate");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.duplicateImage)
    {
        m_dependencies.nonInteractive.duplicateImage(control);
    }
}

void ImageOperationsCoordinator::renameImage() const
{
    auto* control = activeControlOrNotify("Image rename error", "No selected image to rename");
    if (!control)
    {
        return;
    }

    auto* imageRename = new ImageRenameControl(control);
    imageRename->show();
}

void ImageOperationsCoordinator::undo() const
{
    auto* control = activeControlOrNotify("Undo change", "No selected image to undo changes from");
    if (!control)
    {
        return;
    }

    // The view's history restores on a worker and shows the step when it lands; the menu waits for nothing.
    control->undo();
}

void ImageOperationsCoordinator::redo() const
{
    auto* control = activeControlOrNotify("Redo change", "No selected image to redo changes to");
    if (!control)
    {
        return;
    }

    control->redo();
}

void ImageOperationsCoordinator::imageHistory() const
{
    auto* control = activeControlOrNotify("Image History", "No selected image to show history from");
    if (!control)
    {
        return;
    }

    control->history();
}

void ImageOperationsCoordinator::convertToSERSeq() const
{
    auto* control = sequenceControlOrNotify("Convert to SER sequence", "No selected sequence to process");
    if (!control)
    {
        return;
    }

    if (control->isSERSequence())
    {
        notifyError("Convert to SER sequence", "The selected sequence is already in SER format");
        return;
    }

    if (m_dependencies.nonInteractive.convertToSERSequence)
    {
        m_dependencies.nonInteractive.convertToSERSequence(control);
    }
}

void ImageOperationsCoordinator::convertToImageListSeq() const
{
    auto* control = sequenceControlOrNotify("Convert to image list sequence", "No selected sequence to process");
    if (!control)
    {
        return;
    }

    if (control->isImageListSequence())
    {
        notifyError("Convert to Image List sequence", "The selected sequence is already in image list format");
        return;
    }

    if (m_dependencies.nonInteractive.convertToImageListSequence)
    {
        m_dependencies.nonInteractive.convertToImageListSequence(control);
    }
}

void ImageOperationsCoordinator::exportSeq() const
{
    auto* control = sequenceControlOrNotify("Convert to AVI sequence", "No selected sequence to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.exportSequence)
    {
        m_dependencies.exportSequence(control);
    }
}

void ImageOperationsCoordinator::convertToFITSSeq() const
{
    auto* control = sequenceControlOrNotify("Convert to FITS sequence", "No selected sequence to process");
    if (!control)
    {
        return;
    }

    // The engine reads and writes no FITS sequence container, so there is nothing to convert to. Convert to Image
    // List writes every frame as a FITS file.
    notifyError("Convert to FITS sequence", "Converting to a FITS sequence is not available. Convert to Image List "
                                            "writes every frame of the sequence as a FITS file.");
}

void ImageOperationsCoordinator::horizontalMirror() const
{
    auto* control = activeControlOrNotify("Image horizontal mirror", "No selected image to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.horizontalMirror)
    {
        m_dependencies.nonInteractive.horizontalMirror(control);
    }
}

void ImageOperationsCoordinator::verticalMirror() const
{
    auto* control = activeControlOrNotify("Image vertical mirror", "No selected image to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.verticalMirror)
    {
        m_dependencies.nonInteractive.verticalMirror(control);
    }
}

void ImageOperationsCoordinator::rotate180Degree() const
{
    auto* control = activeControlOrNotify("Image rotate", "No selected image to rotate");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.rotate180Degree)
    {
        m_dependencies.nonInteractive.rotate180Degree(control);
    }
}

void ImageOperationsCoordinator::rotate90DegreeRight() const
{
    auto* control = activeControlOrNotify("Image rotate", "No selected image to rotate");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.rotate90DegreeRight)
    {
        m_dependencies.nonInteractive.rotate90DegreeRight(control);
    }
}

void ImageOperationsCoordinator::rotate90DegreeLeft() const
{
    auto* control = activeControlOrNotify("Image rotate", "No selected image to rotate");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.rotate90DegreeLeft)
    {
        m_dependencies.nonInteractive.rotate90DegreeLeft(control);
    }
}

void ImageOperationsCoordinator::negativeTransformation() const
{
    auto* control = activeControlOrNotify("Image negative transformation", "No selected image to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.negativeTransformation)
    {
        m_dependencies.nonInteractive.negativeTransformation(control);
    }
}

void ImageOperationsCoordinator::convertToGrayscale() const
{
    auto* control = activeControlOrNotify("Convert to grayscale", "No selected image to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.convertToGrayscale)
    {
        m_dependencies.nonInteractive.convertToGrayscale(control);
    }
}

void ImageOperationsCoordinator::convertToRGB() const
{
    auto* control = activeControlOrNotify("Convert to RGB", "No selected image to process");
    if (!control)
    {
        return;
    }

    if (m_dependencies.nonInteractive.convertToRGB)
    {
        m_dependencies.nonInteractive.convertToRGB(control);
    }
}

void ImageOperationsCoordinator::zoomIn() const
{
    auto* control = activeControlOrNotify("ImageZoomIn", "No selected image to adjust");
    if (control)
    {
        control->zoomIn();
    }
}

void ImageOperationsCoordinator::zoomOut() const
{
    auto* control = activeControlOrNotify("ImageZoomOut", "No selected image to adjust");
    if (control)
    {
        control->zoomOut();
    }
}

void ImageOperationsCoordinator::zoom100() const
{
    auto* control = activeControlOrNotify("ImageZoom100", "No selected image to adjust");
    if (control)
    {
        control->zoom100();
    }
}

void ImageOperationsCoordinator::fitToWindow() const
{
    auto* control = activeControlOrNotify("ImageFitToWindow", "No selected image to adjust");
    if (control)
    {
        control->fitToWindow();
    }
}

void ImageOperationsCoordinator::switchMaximizeView() const
{
    auto* control = activeControlOrNotify("SwitchMaximizeView", "No selected image to process");
    if (control)
    {
        control->switchMaximized();
    }
}

void ImageOperationsCoordinator::statistics() const
{
    auto* control = activeControlOrNotify("ImageStatistics", "No selected image to process");
    if (control)
    {
        control->executeStatistics();
    }
}

void ImageOperationsCoordinator::histogram() const
{
    auto* control = activeControlOrNotify("ImageHistogram", "No selected image to process");
    if (control)
    {
        control->executeHistogram();
    }
}

void ImageOperationsCoordinator::fitsHeaderViewer() const
{
    auto* control = activeControlOrNotify("ImageFITSHeaderViewer", "No selected image to process");
    if (control)
    {
        control->executeFITSHeaderViewer();
    }
}

void ImageOperationsCoordinator::imageAstrometryInfo() const
{
    auto* control = activeControlOrNotify("ImageAstrometryInfo", "No selected image to process");
    if (control)
    {
        control->executeAstrometryInfoViewer();
    }
}

void ImageOperationsCoordinator::showInFileBrowser() const
{
    auto* control = activeControl();
    if (!control)
    {
        return;
    }

    // The sequence's file for a sequence, otherwise the file the document was read from or last saved to.
    const QString path = QString::fromStdString(control->getFileName());
    if (path.isEmpty())
    {
        return;
    }

    (void)an::ui::showInFileBrowser(messageParent(), path, m_dependencies.revealInFileBrowser);
}

void ImageOperationsCoordinator::exifViewer() const
{
    auto* control = activeControlOrNotify("ImageEXIFViewer", "No selected image to process");
    if (control)
    {
        control->executeEXIFViewer();
    }
}

void ImageOperationsCoordinator::imageSummary() const
{
    auto* control = activeControlOrNotify("ImageSummaryViewer", "No selected image to process");
    if (control)
    {
        control->executeSummaryViewer();
    }
}

void ImageOperationsCoordinator::plateSolve() const
{
    auto* control = activeControlOrNotify("PlateSolve", "No selected image to process");
    if (control)
    {
        control->executePlateSolve();
    }
}

void ImageOperationsCoordinator::fitImageAllWindows() const
{
    auto* active = activeControl();
    if (!canRunFitAllWindows(active != nullptr))
    {
        return;
    }

    auto controls = listViewControls();
    for (auto* control : controls)
    {
        if (control)
        {
            control->fitToWindow();
        }
    }
}

ImageViewControl* ImageOperationsCoordinator::activeControl() const
{
    if (!m_dependencies.activeViewControl)
    {
        return nullptr;
    }

    return m_dependencies.activeViewControl();
}

ImageViewControl* ImageOperationsCoordinator::activeControlOrNotify(const QString& title, const QString& message) const
{
    auto* control = activeControl();
    if (!canRunActiveImageOperation(control != nullptr))
    {
        notifyError(title, message);
        return nullptr;
    }

    return control;
}

ImageViewControl* ImageOperationsCoordinator::sequenceControlOrNotify(const QString& title,
                                                                      const QString& message) const
{
    auto* control = activeControl();
    if (!canRunSequenceOperation(control != nullptr, control ? control->isSequence() : false))
    {
        notifyError(title, message);
        return nullptr;
    }

    return control;
}

std::vector<ImageViewControl*> ImageOperationsCoordinator::listViewControls() const
{
    if (!m_dependencies.listViewControls)
    {
        return {};
    }

    return m_dependencies.listViewControls();
}

QWidget* ImageOperationsCoordinator::messageParent() const
{
    if (!m_dependencies.messageParent)
    {
        return nullptr;
    }

    return m_dependencies.messageParent();
}

void ImageOperationsCoordinator::notifyError(const QString& title, const QString& message) const
{
    if (m_dependencies.notifyErrorMessage)
    {
        m_dependencies.notifyErrorMessage(title, message);
    }
}

} // namespace an::ui
