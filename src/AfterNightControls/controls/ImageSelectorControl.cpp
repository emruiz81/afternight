// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageSelectorControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "app/UiStyleTokens.h"

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

} // anonymous namespace

ImageSelectorControl::ImageSelectorControl(QWidget* parent, const std::unordered_set<QString>& selectedItems)
    : QDialog(parent), AfterNightAppListener(appEventBus())
{
    m_ui = an::ui::controls::buildImageSelectorControlUi(this);
    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageSelectorControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.listWidget, &QListWidget::itemChanged, this, &ImageSelectorControl::itemChanged);

    QObject::connect(m_ui.selectAllButton, &QPushButton::released, this,
                     &ImageSelectorControl::on_selectAllButtonPushed);
    QObject::connect(m_ui.clearAllButton, &QPushButton::released, this, &ImageSelectorControl::on_clearAllButtonPushed);

    std::string title = fmt::format("Image Selector Control");
    setWindowTitle(QString::fromUtf8(title));

    m_OriginalSelectedImages = m_SelectedImages = selectedItems;
    initialize();
}

void ImageSelectorControl::onViewControlCreated(const ImageViewControl* ctrl)
{
    auto imageName = QString::fromUtf8(ctrl->getUniqueName());

    m_ui.listWidget->addItem(imageName);

    int idx = findItem(imageName);
    if (idx != -1)
    {
        auto item = m_ui.listWidget->item(idx);

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
}

void ImageSelectorControl::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    auto imageName = QString::fromUtf8(ctrl->getUniqueName());

    auto it = m_SelectedImages.find(imageName);
    if (it != m_SelectedImages.end())
    {
        m_SelectedImages.erase(it);
    }

    auto it2 = m_OriginalSelectedImages.find(imageName);
    if (it2 != m_OriginalSelectedImages.end())
    {
        m_OriginalSelectedImages.erase(it2);
    }

    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);

        if (item->text() == imageName)
        {
            m_ui.listWidget->removeItemWidget(item);
            break;
        }
    }
}

void ImageSelectorControl::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                const ImageViewControl* ctrl)
{
    auto newImageName = QString::fromUtf8(newName);
    auto oldImageName = QString::fromUtf8(oldName);

    auto it = m_SelectedImages.find(oldImageName);
    if (it != m_SelectedImages.end())
    {
        m_SelectedImages.erase(it);
        m_SelectedImages.insert(newImageName);
    }

    auto it2 = m_OriginalSelectedImages.find(oldImageName);
    if (it2 != m_OriginalSelectedImages.end())
    {
        m_OriginalSelectedImages.erase(it2);
        m_OriginalSelectedImages.insert(newImageName);
    }

    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);

        if (item->text() == oldImageName)
        {
            item->setText(newImageName);
            break;
        }
    }
}

void ImageSelectorControl::initialize()
{
    m_IgnoreLastItemChangedEvent = true;
    m_ui.listWidget->clear();

    if (auto* manager = viewControlManager())
    {
        for (auto* viewControl : manager->getListOfViewControls())
        {
            if (!viewControl)
            {
                continue;
            }

            // TODO: Apply filters here!
            m_ui.listWidget->addItem(QString::fromUtf8(viewControl->getUniqueName()));
        }
    }

    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        if (m_SelectedImages.find(item->text()) != m_SelectedImages.end())
        {
            item->setCheckState(Qt::Checked);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        else
        {
            item->setCheckState(Qt::Unchecked);
        }
    }

    if (m_ui.listWidget->count() == 0)
    {
        m_ui.selectAllButton->setEnabled(false);
        m_ui.clearAllButton->setEnabled(false);
    }
    else
    {
        m_ui.selectAllButton->setEnabled(true);
        m_ui.clearAllButton->setEnabled(true);
    }

    m_IgnoreLastItemChangedEvent = false;
}

QStringList ImageSelectorControl::getSelectedImages() const
{
    QStringList selectedImages;
    selectedImages.reserve(m_SelectedImages.size());
    for (auto& img : m_SelectedImages)
    {
        selectedImages.append(img);
    }

    return selectedImages;
}

ImageSelectorControl::~ImageSelectorControl() {}

void ImageSelectorControl::on_selectAllButtonPushed()
{
    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);
        if (m_OriginalSelectedImages.find(item->text()) == m_OriginalSelectedImages.end())
        {
            item->setCheckState(Qt::Checked);
        }
    }
}

void ImageSelectorControl::on_clearAllButtonPushed()
{
    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);
        if (m_OriginalSelectedImages.find(item->text()) == m_OriginalSelectedImages.end())
        {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void ImageSelectorControl::itemChanged(QListWidgetItem* item)
{
    if (m_IgnoreLastItemChangedEvent)
        return;

    const QColor selectedBackground =
        an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::HighlightBackground);
    const QColor unselectedBackground =
        an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::SurfaceWindow);

    if (item->checkState() == Qt::Checked)
    {
        item->setBackground(selectedBackground);
        m_SelectedImages.insert(item->text());
    }
    else
    {
        item->setBackground(unselectedBackground);
        auto it = m_SelectedImages.find(item->text());
        if (it != m_SelectedImages.end())
        {
            m_SelectedImages.erase(it);
        }
    }
}

int ImageSelectorControl::findItem(const QString& text)
{
    QListWidgetItem* item = 0;
    for (int i = 0; i < m_ui.listWidget->count(); ++i)
    {
        item = m_ui.listWidget->item(i);
        if (item->text() == text)
        {
            return i;
        }
    }

    return -1;
}

void ImageSelectorControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        this->accept();
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->reject();
    }
}
