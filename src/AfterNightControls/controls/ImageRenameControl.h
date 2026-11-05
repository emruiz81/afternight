// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "ImageRenameControlUiBuilder.h"
#include "controls/ImageViewControlListener.h"

class AFTERNIGHTCONTROLS_API ImageRenameControl : public QDialog, public ImageViewControlListener
{
public:
    ImageRenameControl(ImageViewControl* control);

    void onImageRenamed(ImageViewControl* ctrl) override;

    ~ImageRenameControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    bool executeRename();

    an::ui::controls::ImageRenameControlUiElements m_ui;
};
