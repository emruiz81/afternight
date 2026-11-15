// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

class ImageViewControl;
class QString;

class AFTERNIGHTCONTROLS_API ImageViewControlListener
{
public:
    ImageViewControlListener(ImageViewControl* ctrl);

    void setViewControl(ImageViewControl* ctrl);

    virtual void onPreImageLoaded(ImageViewControl* ctrl) {}

    virtual void onImageLoaded(ImageViewControl* ctrl) {}

    virtual void onImageLoadFailed(ImageViewControl* ctrl, const QString& message) {}

    virtual void onPostImageLoaded(ImageViewControl* ctrl) {}

    virtual void onPreImageSaved(ImageViewControl* ctrl) {}

    virtual void onImageSaved(ImageViewControl* ctrl) {}

    virtual void onPostImageSaved(ImageViewControl* ctrl) {}

    virtual void onPreImageUpdated(ImageViewControl* ctrl) {}

    virtual void onImageUpdated(ImageViewControl* ctrl) {}

    virtual void onPostImageUpdated(ImageViewControl* ctrl) {}

    virtual void onPreImageRenamed(ImageViewControl* ctrl) {}

    virtual void onImageRenamed(ImageViewControl* ctrl) {}

    virtual void onPostImageRenamed(ImageViewControl* ctrl) {}

    virtual void onImageViewControlDestroyed(ImageViewControl* ctrl);

    virtual void onMaskUpdated(ImageViewControl* ctrl) {}

    virtual void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) {}

    virtual void onAlternateImageEnabled(ImageViewControl* ctrl) {}

    virtual void onAlternateImageDisabled(ImageViewControl* ctrl) {}

    virtual void onImageViewScaled(ImageViewControl* ctrl) {}

    virtual void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) {}

    /// The view's processing lock was taken or let go (`ImageViewControl::tryRequestLockForProcessing`). Told on the
    /// GUI thread, whichever thread took or let go the lock, with the lock's state when it is told; a lock taken and
    /// let go before the GUI thread could tell is not told.
    virtual void onProcessingLockChanged(ImageViewControl* ctrl, bool locked) {}

    /// What a toolbar shows of the view changed: a choice offered or withdrawn (`ImageViewControl::chromeActions`).
    virtual void onChromeActionsChanged(ImageViewControl* ctrl) {}

    // Only for RT preview controls
    virtual void onRTTakeSnapshot(ImageViewControl* ctrl) {}

    virtual ~ImageViewControlListener();

    ImageViewControl* getParentViewControl() const
    {
        return m_image_view_control;
    }

protected:
    ImageViewControl* m_image_view_control;

private:
};
