// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "ImageMaskSelectorControlUiBuilder.h"
#include "controls/ImageViewControlListener.h"
#include "app/AfterNightAppListener.h"

class QWidget;

class AFTERNIGHTCONTROLS_API ImageMaskSelectorControl : public QDialog,
                                                        public AfterNightAppListener,
                                                        public ImageViewControlListener
{
public:
    explicit ImageMaskSelectorControl(QWidget* parent, ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    virtual void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    ~ImageMaskSelectorControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void recalculateMasksList();

    an::ui::controls::ImageMaskSelectorControlUiElements m_ui;
};
