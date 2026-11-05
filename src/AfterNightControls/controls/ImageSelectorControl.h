// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "ImageSelectorControlUiBuilder.h"
#include "app/AfterNightAppListener.h"

class ImageViewControl;
class QWidget;

class ImageSelectorControl : public QDialog, public AfterNightAppListener
{
public:
    explicit ImageSelectorControl(QWidget* parent = nullptr,
                                  const std::unordered_set<QString>& selectedItems = std::unordered_set<QString>());

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void initialize();

    QStringList getSelectedImages() const;

    ~ImageSelectorControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_selectAllButtonPushed();
    void on_clearAllButtonPushed();

    void itemChanged(QListWidgetItem* item);

    int findItem(const QString& text);

    std::unordered_set<QString> m_SelectedImages;
    std::unordered_set<QString> m_OriginalSelectedImages;

    bool m_IgnoreLastItemChangedEvent = false;

    an::ui::controls::ImageSelectorControlUiElements m_ui;
};
