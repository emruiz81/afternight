// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QMainWindow>
#include "ImageSummaryViewerControlUiBuilder.h"
#include "controls/ImageViewControlListener.h"

class ImageViewControl;

/// The summary of the image or the sequence a view shows and of the file behind it.
///
/// Reads the view's document on the GUI thread under a lease that never waits: while a writer holds the image the
/// summary keeps what it shows and is built again at the view's next update.
class ImageSummaryViewerControl : public QDialog, public ImageViewControlListener
{
public:
    ImageSummaryViewerControl(ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    /// Builds the summary again from the view's document.
    void recalculate();

    /// The summary as plain text, which Copy puts on the clipboard.
    [[nodiscard]] const QString& reportPlainText() const
    {
        return m_reportPlainText;
    }

    ~ImageSummaryViewerControl();

private:
    void on_closeButtonPushed();

    void on_copyButtonPushed();

    virtual void done(int r) override;

    void initialize();

    QString m_reportPlainText;

    an::ui::controls::ImageSummaryViewerControlUiElements m_ui;
};
