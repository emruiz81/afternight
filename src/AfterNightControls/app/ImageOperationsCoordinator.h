// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>
#include <vector>

#include <QString>

class ImageViewControl;
class QWidget;

namespace an::ui
{

inline bool shouldSkipCloseCandidateWhenClosingOthers(const bool candidateIsActive)
{
    return candidateIsActive;
}

inline bool canRunActiveImageOperation(const bool hasActiveControl)
{
    return hasActiveControl;
}

inline bool canRunSequenceOperation(const bool hasActiveControl, const bool isSequence)
{
    return hasActiveControl && isSequence;
}

inline bool canRunFitAllWindows(const bool hasActiveControl)
{
    return hasActiveControl;
}

class AFTERNIGHTCONTROLS_API ImageOperationsCoordinator
{
public:
    struct NonInteractiveOperations
    {
        std::function<void(ImageViewControl*)> duplicateImage;
        std::function<void(ImageViewControl*)> convertToSERSequence;
        std::function<void(ImageViewControl*)> convertToImageListSequence;
        std::function<void(ImageViewControl*)> horizontalMirror;
        std::function<void(ImageViewControl*)> verticalMirror;
        std::function<void(ImageViewControl*)> rotate180Degree;
        std::function<void(ImageViewControl*)> rotate90DegreeRight;
        std::function<void(ImageViewControl*)> rotate90DegreeLeft;
        std::function<void(ImageViewControl*)> negativeTransformation;
        std::function<void(ImageViewControl*)> convertToGrayscale;
        std::function<void(ImageViewControl*)> convertToRGB;
    };

    struct Dependencies
    {
        std::function<ImageViewControl*()> activeViewControl;
        std::function<std::vector<ImageViewControl*>()> listViewControls;
        std::function<QWidget*()> messageParent;
        std::function<void(const QString&, const QString&)> notifyErrorMessage;
        NonInteractiveOperations nonInteractive;

        /// Opens the export dialog over a sequence view; the dialog belongs to the sequence controls.
        std::function<void(ImageViewControl*)> exportSequence;

        /// Shows a file in the system file manager with it selected; when empty, the system's file manager is asked.
        std::function<bool(const QString&)> revealInFileBrowser;
    };

    explicit ImageOperationsCoordinator(const Dependencies& dependencies);

    void closeFile() const;
    void closeAllFiles() const;
    void closeAllOtherFiles() const;

    void duplicateImage() const;
    void renameImage() const;
    void undo() const;
    void redo() const;
    void imageHistory() const;

    /// Shows the file the active view's image was read from or last saved to - a sequence's file for a sequence - in
    /// the system file manager, with it selected; says so when the file is no longer on disk, and does nothing for an
    /// image that has never been a file.
    void showInFileBrowser() const;

    void convertToSERSeq() const;
    void convertToImageListSeq() const;
    void exportSeq() const;
    void convertToFITSSeq() const;

    void horizontalMirror() const;
    void verticalMirror() const;
    void rotate180Degree() const;
    void rotate90DegreeRight() const;
    void rotate90DegreeLeft() const;
    void negativeTransformation() const;
    void convertToGrayscale() const;
    void convertToRGB() const;

    void zoomIn() const;
    void zoomOut() const;
    void zoom100() const;
    void fitToWindow() const;
    void switchMaximizeView() const;

    void statistics() const;
    void histogram() const;
    void fitsHeaderViewer() const;
    void imageAstrometryInfo() const;
    void exifViewer() const;
    void imageSummary() const;
    void plateSolve() const;

    void fitImageAllWindows() const;

private:
    ImageViewControl* activeControl() const;
    ImageViewControl* activeControlOrNotify(const QString& title, const QString& message) const;
    ImageViewControl* sequenceControlOrNotify(const QString& title, const QString& message) const;
    std::vector<ImageViewControl*> listViewControls() const;
    QWidget* messageParent() const;
    void notifyError(const QString& title, const QString& message) const;

    Dependencies m_dependencies;
};

} // namespace an::ui
