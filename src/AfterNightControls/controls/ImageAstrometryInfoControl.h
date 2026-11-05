// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/AfterNightAppListener.h"
#include "controls/ImageViewControlListener.h"

#include <QPointer>
#include <QtWidgets/QMainWindow>

class ImageViewControl;
class QLabel;
class QPushButton;

namespace an::ui
{
class AnCardWidget;

class ImageAstrometryInfoControl : public QDialog, public AfterNightAppListener, public ImageViewControlListener
{
public:
    explicit ImageAstrometryInfoControl(ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;
    void onActiveViewControlChanged(ImageViewControl* ctrl) override;

    ~ImageAstrometryInfoControl() override;

private:
    void attachToViewControl(ImageViewControl* control);
    void refreshWindowTitle();
    void recalculate();
    void setRowValue(QLabel* label, const QString& value) const;

    QLabel* m_statusLabel = nullptr;
    an::ui::AnCardWidget* m_summaryCard = nullptr;
    QLabel* m_fieldCenterRaLabel = nullptr;
    QLabel* m_fieldCenterDecLabel = nullptr;
    QLabel* m_crvalRaLabel = nullptr;
    QLabel* m_crvalDecLabel = nullptr;
    QLabel* m_imageScaleLabel = nullptr;
    QLabel* m_rotationLabel = nullptr;
    QLabel* m_fieldOfViewLabel = nullptr;
    QLabel* m_projectionLabel = nullptr;
    QLabel* m_equinoxLabel = nullptr;
    QLabel* m_sipLabel = nullptr;
    QLabel* m_solverLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPointer<ImageViewControl> m_currentViewControl;
};

} // namespace an::ui
