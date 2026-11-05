// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageRenameControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"

ImageRenameControl::ImageRenameControl(ImageViewControl* control) : QDialog(control), ImageViewControlListener(control)
{
    m_ui = an::ui::controls::buildImageRenameControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageRenameControl::on_dialog_box_button_clicked);

    auto uniqueName = m_image_view_control->getUniqueName();

    auto title = fmt::format("Rename <{}>", uniqueName);
    setWindowTitle(QString::fromUtf8(title));
}

void ImageRenameControl::onImageRenamed(ImageViewControl* ctrl)
{
    std::string title = fmt::format("Rename <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));
}

ImageRenameControl::~ImageRenameControl() {}

void ImageRenameControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        if (executeRename())
        {
            this->close();
        }
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

bool ImageRenameControl::executeRename()
{
    auto* context = an::ui::appContext();
    auto* dialogService = context ? context->getDialogService() : nullptr;
    auto* viewManager = context ? context->getViewControlManager() : nullptr;
    auto* viewRegistry = context ? context->getViewRegistry() : nullptr;
    auto newName = m_ui.newImageName->text().toUtf8().toStdString();

    if (newName.empty())
    {
        auto message = fmt::format("Image name is invalid");
        if (dialogService)
        {
            dialogService->notifyWarnMessage(this, "Image Rename", QString::fromUtf8(message));
        }

        return false;
    }

    if (viewManager)
    {
        if (auto* existingControl = viewManager->getViewControlByName(newName))
        {
            if (existingControl != m_image_view_control)
            {
                auto message = fmt::format("Image with name '{}' already exists", newName);
                if (dialogService)
                {
                    dialogService->notifyWarnMessage(this, "Image Rename", QString::fromUtf8(message));
                }

                return false;
            }
        }
    }

    m_image_view_control->setUniqueName(newName);
    if (viewRegistry)
    {
        viewRegistry->setActiveImageViewControl(m_image_view_control);
    }
    if (auto* history = m_image_view_control->viewHistory())
    {
        history->registerRename();
    }

    return true;
}
