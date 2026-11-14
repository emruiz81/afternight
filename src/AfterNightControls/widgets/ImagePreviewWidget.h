// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QWidget>
#include "AfterNightControlsExport.h"
#include "ImageLensWidget.h"
#include "theme/NightImage.h"

#include <memory>

namespace an::consumer
{
class ImageAsset;
} // namespace an::consumer

namespace an::ui
{
class PresentedImage;
} // namespace an::ui

/// A preview of an image painted from a pixmap, with the quick-zoom lens over it.
///
/// The lens magnifies what the preview shows - the presented bytes its owner hands it - and reads the values under the
/// cursor from the image itself.
class AFTERNIGHTCONTROLS_API ImagePreviewWidget : public QWidget
{
public:
    ImagePreviewWidget(QWidget* parent);

    // Different from parent... indicates the main widget (window or dialog) that contains this widget
    void setOwnerWidget(QWidget* owner);

    void setPixmap(const QPixmap& pixmap, bool scaled);

    /// What the lens magnifies and reads: the presented bytes the pixmap was made from, and the image they present.
    /// Either may be null; the lens then shows nothing of it.
    void setPreview(std::shared_ptr<const an::ui::PresentedImage> shown,
                    std::shared_ptr<const an::consumer::ImageAsset> source);

    void setQuickZoomEnabled(const bool enabled);

    void setBackgroundColor(const QColor& color);

    QColor getBackgroundColor() const;

    ~ImagePreviewWidget();

private:
    void resizeEvent(QResizeEvent* evt) override
    {
        QWidget::resizeEvent(evt);
    }

    void paintEvent(QPaintEvent* evt) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void changeEvent(QEvent* ev) override;

    void updateQuickZoomWidget(const QPoint& mousePos);
    void showImageLensWidget();

    QRect getScaledImageArea();

    QPixmap m_MainPixmap;
    /// The pixmap as it is painted: under Night in its Night form, made off the GUI thread.
    an::ui::NightPicture m_ShownPicture;
    bool m_DrawScaled = false;

    QColor m_BackgroundColor;

    std::unique_ptr<ImageLensWidget> m_ImageLensWidget;
    bool m_QuickZoomEnabled = true;
    bool m_MouseLeftButtonPressed = false;

    QTimer* m_QuickZoomTimer = nullptr;
    int m_QuickZoomDelayMs = 300;

    QWidget* m_Owner = nullptr;

    std::shared_ptr<const an::ui::PresentedImage> m_Shown;
    std::shared_ptr<const an::consumer::ImageAsset> m_Source;
};

class NoImagePreviewWidget : public QWidget
{
public:
    NoImagePreviewWidget(QWidget* parent);

    void setPixmap(const QPixmap& pixmap, bool scaled);

    void setBackgroundColor(const QColor& color);

    QColor getBackgroundColor() const;

    ~NoImagePreviewWidget();

private:
    void paintEvent(QPaintEvent* evt) override;
    void changeEvent(QEvent* ev) override;

    QRect getScaledImageArea();

    QPixmap m_MainPixmap;
    /// The pixmap as it is painted: under Night in its Night form, made off the GUI thread.
    an::ui::NightPicture m_ShownPicture;
    bool m_DrawScaled = false;

    QColor m_BackgroundColor;
};
