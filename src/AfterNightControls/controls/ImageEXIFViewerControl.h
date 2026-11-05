// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QDialog>
#include "ImageEXIFViewerControlUiBuilder.h"
#include "controls/ImageViewControlListener.h"

class ImageViewControl;

/// The Exif fields the image a view shows was read with, as tag, value and full key.
///
/// Reads the view's document on the GUI thread under a lease that never waits: while a writer holds the image the
/// table keeps what it shows and is filled again at the view's next update.
class ImageEXIFViewerControl : public QDialog, public ImageViewControlListener
{
public:
    ImageEXIFViewerControl(ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    /// Fills the table again from the view's document.
    void recalculate();

    ~ImageEXIFViewerControl();

private:
    void on_closeButtonPushed();

    virtual void done(int r) override;

    void initialize();

    an::ui::controls::ImageEXIFViewerControlUiElements m_ui;
};
