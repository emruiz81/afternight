// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageMaskSelectorControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"

namespace
{

an::ui::IAppEventBus* appEventBus()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getAppEventBus();
    }

    return nullptr;
}

an::ui::IViewControlManager* viewControlManager()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewControlManager();
    }

    return nullptr;
}

an::ui::IActionStateManager* actionStateManager()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getActionStateManager();
    }

    return nullptr;
}

} // anonymous namespace

ImageMaskSelectorControl::ImageMaskSelectorControl(QWidget* parent, ImageViewControl* control)
    : QDialog(control), AfterNightAppListener(appEventBus()), ImageViewControlListener(control)
{
    Q_UNUSED(parent);
    m_ui = an::ui::controls::buildImageMaskSelectorControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageMaskSelectorControl::on_dialog_box_button_clicked);

    // QObject::connect(this->maskSelector, &QComboBox::currentTextChanged, this,
    // &ImageMaskSelectorControl::on_maskSelector_currentTextChanged);

    std::string title = fmt::format("Mask Selector <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));

    recalculateMasksList();

    auto mask = m_image_view_control->getCurrentMask();
    if (mask)
    {
        m_ui.currentMask->setText(QString::fromUtf8(mask->getUniqueName()));
    }
    else
    {
        m_ui.currentMask->setText("NO MASK");
    }
}

void ImageMaskSelectorControl::onImageLoaded(ImageViewControl* ctrl)
{
    recalculateMasksList();
}

void ImageMaskSelectorControl::onImageUpdated(ImageViewControl* ctrl)
{
    // Already done in onViewControlUpdated()
    // recalculateMasksList();
}

void ImageMaskSelectorControl::onImageRenamed(ImageViewControl* ctrl)
{
    std::string title = fmt::format("Mask Selector <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));
}

void ImageMaskSelectorControl::onViewControlCreated(const ImageViewControl* ctrl)
{
    recalculateMasksList();
}

void ImageMaskSelectorControl::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    recalculateMasksList();
}

void ImageMaskSelectorControl::onViewControlUpdated(const ImageViewControl* ctrl)
{
    recalculateMasksList();
}

void ImageMaskSelectorControl::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                    const ImageViewControl* ctrl)
{
    recalculateMasksList();
}

ImageMaskSelectorControl::~ImageMaskSelectorControl() {}

void ImageMaskSelectorControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        auto newMask = m_ui.maskSelector->currentText().toUtf8().toStdString();

        auto currentControlMask = m_image_view_control->getCurrentMask();

        std::string currentControlMaskStr;
        if (currentControlMask)
        {
            currentControlMaskStr = currentControlMask->getUniqueName();
        }

        if (newMask != "<NO COMPATIBLE IMAGES>" && newMask != currentControlMaskStr)
        {
            auto* manager = viewControlManager();
            auto* newControlMask = manager ? manager->getViewControlByName(newMask) : nullptr;
            if (!newControlMask)
            {
                this->close();
                return;
            }

            m_image_view_control->setCurrentMask(newControlMask);

            LOG_INFO("Applied mask '{}' to image '{}'", newControlMask->getUniqueName(),
                     m_image_view_control->getUniqueName());

            if (auto* actionState = actionStateManager())
            {
                actionState->updateActiveImageActions();
            }
        }

        this->close();
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

void ImageMaskSelectorControl::recalculateMasksList()
{
    m_ui.maskSelector->clear();
    m_ui.maskSelector->setEnabled(true);

    const std::optional<an::ImageDescriptor> source = m_image_view_control->imageDescriptor();
    if (!source.has_value())
    {
        m_ui.maskSelector->addItem("<NO COMPATIBLE IMAGES>");
        m_ui.maskSelector->setItemData(0, QString(), Qt::ToolTipRole);
        m_ui.maskSelector->setEnabled(false);
        m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    const bool srcIsRGB = source->extent().channelCount() >= 3u;

    if (auto* manager = viewControlManager())
    {
        for (auto* viewControl : manager->getListOfViewControls())
        {
            if (!viewControl)
            {
                continue;
            }

            const std::string image = viewControl->getUniqueName();

            // A view control can't be its own mask!
            if (image == m_image_view_control->getUniqueName())
            {
                continue;
            }

            const std::optional<an::ImageDescriptor> mask = viewControl->imageDescriptor();
            if (!mask.has_value())
            {
                continue;
            }
            // A mask has one channel or three: a four-channel image would be tinted as four layers, its alpha among
            // them. A colour mask is only valid on a colour image; a grey mask is valid on both.
            const std::uint32_t maskChannels = mask->extent().channelCount();
            if (maskChannels != 1u && maskChannels != 3u)
            {
                continue;
            }
            if (!srcIsRGB && maskChannels == 3u)
            {
                continue;
            }

            // A mask needs to have the exact same size
            if (mask->extent().width() != source->extent().width() ||
                mask->extent().height() != source->extent().height())
            {
                continue;
            }

            // Avoid cross referenced masks
            if (viewControl->getCurrentMask() == m_image_view_control)
            {
                continue;
            }

            m_ui.maskSelector->addItem(QString::fromUtf8(image));
            const int itemIndex = m_ui.maskSelector->count() - 1;
            m_ui.maskSelector->setItemData(itemIndex, an::ui::buildImageSelectorTooltip(viewControl), Qt::ToolTipRole);
        }
    }

    if (!m_ui.maskSelector->count())
    {
        m_ui.maskSelector->addItem("<NO COMPATIBLE IMAGES>");
        m_ui.maskSelector->setItemData(0, QString(), Qt::ToolTipRole);
        m_ui.maskSelector->setEnabled(false);

        m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
}
