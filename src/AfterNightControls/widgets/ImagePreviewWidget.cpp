// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImagePreviewWidget.h"
#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/PresentationBridge.h"
#include "widgets/QuickZoomController.h"
#include "widgets/ViewPresentation.h"
#include "common.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <optional>

namespace
{

QPixmap loadNoImagePreviewPixmap()
{
    const QSize icon_size(64, 64);

    if (auto* ctx = an::ui::appContext())
    {
        if (auto provider = ctx->getIconProvider())
        {
            const QColor color = provider->getColorNormal().isValid() ? provider->getColorNormal()
                                                                      : an::ui::tokens().colors.textSecondary;
            const QPixmap pixmap =
                provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/picture.svg"), icon_size, color);
            if (!pixmap.isNull())
            {
                return pixmap;
            }
        }
    }

    return QIcon(QStringLiteral(":/AfterNight/icons/SVG/picture.svg")).pixmap(icon_size);
}

} // anonymous namespace

ImagePreviewWidget::ImagePreviewWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_DeleteOnClose);

    m_BackgroundColor = an::ui::tokens().colors.bgPrimary;

    m_ImageLensWidget = std::make_unique<ImageLensWidget>(nullptr);
    m_ImageLensWidget->hide();

    m_QuickZoomTimer = new QTimer(this);

    connect(m_QuickZoomTimer, &QTimer::timeout, this, &ImagePreviewWidget::showImageLensWidget);
    m_QuickZoomTimer->setSingleShot(true);

    setCursor(createSolidCrossCursor());
}

void ImagePreviewWidget::setOwnerWidget(QWidget* owner)
{
    m_Owner = owner;
}

void ImagePreviewWidget::setPixmap(const QPixmap& pixmap, bool scaled)
{
    m_MainPixmap = pixmap;
    m_DrawScaled = scaled;
    update();
}

void ImagePreviewWidget::setPreview(std::shared_ptr<const an::ui::PresentedImage> shown,
                                    std::shared_ptr<const an::consumer::ImageAsset> source)
{
    m_Shown = std::move(shown);
    m_Source = std::move(source);
}

void ImagePreviewWidget::setQuickZoomEnabled(const bool enabled)
{
    m_QuickZoomEnabled = enabled;
}

void ImagePreviewWidget::setBackgroundColor(const QColor& color)
{
    m_BackgroundColor = color;
    update();
}

QColor ImagePreviewWidget::getBackgroundColor() const
{
    return m_BackgroundColor;
}

ImagePreviewWidget::~ImagePreviewWidget()
{
    m_ImageLensWidget->detachFromOwner();
}

void ImagePreviewWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
    {
        m_BackgroundColor = an::ui::tokens().colors.bgPrimary;
        update();
    }
    QWidget::changeEvent(ev);
}

void ImagePreviewWidget::paintEvent(QPaintEvent* evt)
{
    if (m_MainPixmap.isNull())
    {
        return;
    }

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(this->rect(), m_BackgroundColor);

    if (m_DrawScaled)
    {
        auto targetRect = getScaledImageArea();

        painter.drawPixmap(targetRect, m_MainPixmap);
    }
    else
    {
        QSize widgetSize = size();
        QSize pixmapSize = m_MainPixmap.size();

        // Calculate the top-left corner for centering the pixmap
        int x = (widgetSize.width() - pixmapSize.width()) / 2;
        int y = (widgetSize.height() - pixmapSize.height()) / 2;

        // Draw the pixmap centered and without scaling
        painter.drawPixmap(x, y, m_MainPixmap);
    }
}

void ImagePreviewWidget::mousePressEvent(QMouseEvent* ev)
{
    if (m_QuickZoomEnabled && ev->button() == Qt::LeftButton)
    {
        ev->accept();

        m_MouseLeftButtonPressed = true;

        // TODO: Check if mouse cursor is actually inside the image area
        if (getScaledImageArea().contains(this->mapFromGlobal(QCursor::pos())))
        {
            updateQuickZoomWidget(ev->pos());
            m_QuickZoomTimer->start(m_QuickZoomDelayMs); // Adjust the delay as needed
        }
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();

        if (m_MouseLeftButtonPressed && m_QuickZoomEnabled)
        {
            updateQuickZoomWidget(mapFromGlobal(QCursor::pos()));
        }
    }

    QWidget::mousePressEvent(ev);
}

void ImagePreviewWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_QuickZoomEnabled && ev->button() == Qt::LeftButton)
    {
        ev->accept();

        m_MouseLeftButtonPressed = false;
        m_QuickZoomTimer->stop(); // Stop the timer if the button is released
        m_ImageLensWidget->hide();
        m_ImageLensWidget->detachFromOwner();
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();

        if (m_MouseLeftButtonPressed && m_QuickZoomEnabled)
        {
            updateQuickZoomWidget(ev->pos());
        }
    }

    QWidget::mouseReleaseEvent(ev);
}

void ImagePreviewWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_QuickZoomEnabled && m_MouseLeftButtonPressed)
    {
        ev->accept();

        if (!getScaledImageArea().contains(this->mapFromGlobal(QCursor::pos())))
        {
            m_ImageLensWidget->hide();
        }
        else
        {
            updateQuickZoomWidget(ev->pos());
        }
    }

    QWidget::mouseMoveEvent(ev);
}

void ImagePreviewWidget::updateQuickZoomWidget(const QPoint& mousePos)
{
    if (!m_Shown || !m_Owner)
        return;

    const QPoint ownerCursorPos = m_Owner->mapFromGlobal(QCursor::pos());
    QPoint newPos = an::ui::calculateQuickZoomLensPosition(ownerCursorPos, m_ImageLensWidget->size(), m_Owner->size());

    // The lens works in the shown image's pixels; the values it reads out are the source's, at the same place.
    const auto scaledArea = getScaledImageArea();
    const double currentScale = static_cast<double>(scaledArea.width()) / static_cast<double>(m_Shown->width());
    const auto region =
        an::ui::calculateQuickZoomRegion(mousePos, scaledArea.topLeft(), currentScale,
                                         static_cast<int>(m_Shown->width()), static_cast<int>(m_Shown->height()));

    if (region.cropRect.isEmpty())
    {
        return;
    }

    const QImage shown = an::ui::presentedQImage(m_Shown);
    m_ImageLensWidget->m_CurrentPixmap = QPixmap::fromImage(shown.copy(region.cropRect));

    if (auto* context = an::ui::appContext())
    {
        if (auto* provider = context->getEngineSessionProvider())
        {
            // The lens never waits on the GUI thread: with no thread free, it keeps the counts it showed last.
            an::Result<an::consumer::OperationScope> scope = provider->engineSession().tryAdmit(
                an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Background});
            if (scope.hasValue())
            {
                const an::Result<an::ui::LensHistogram> counted = an::ui::countPresentedRegion(
                    scope->execution(), *m_Shown, static_cast<std::uint32_t>(region.cropRect.x()),
                    static_cast<std::uint32_t>(region.cropRect.y()),
                    static_cast<std::uint32_t>(region.cropRect.width()),
                    static_cast<std::uint32_t>(region.cropRect.height()));
                if (counted.hasValue())
                {
                    m_ImageLensWidget->setHistogram(*counted);
                }
            }
        }
    }

    const double absolutePosX = region.absolutePosition.x();
    const double absolutePosY = region.absolutePosition.y();
    m_ImageLensWidget->m_CurrentX = absolutePosX;
    m_ImageLensWidget->m_CurrentY = absolutePosY;

    std::optional<an::ui::PixelReadout> readout;
    if (m_Source)
    {
        if (an::Result<an::consumer::AssetReadLease> lease = m_Source->tryRead(); lease.hasValue())
        {
            const std::optional<an::ImageDescriptor> source = m_Source->descriptor();
            const double toSourceX =
                source ? static_cast<double>(source->extent().width()) / static_cast<double>(m_Shown->width()) : 1.0;
            const double toSourceY =
                source ? static_cast<double>(source->extent().height()) / static_cast<double>(m_Shown->height()) : 1.0;
            readout = an::ui::readPixel(*lease, static_cast<std::int64_t>(absolutePosX * toSourceX),
                                        static_cast<std::int64_t>(absolutePosY * toSourceY));
        }
    }
    if (!readout.has_value())
    {
        readout = an::ui::readPresentedPixel(*m_Shown, static_cast<std::int64_t>(absolutePosX),
                                             static_cast<std::int64_t>(absolutePosY));
    }
    m_ImageLensWidget->isRGB = readout->channels >= 3u;
    m_ImageLensWidget->m_CurrentChannel0 = readout->values[0];
    m_ImageLensWidget->m_CurrentChannel1 = readout->values[1];
    m_ImageLensWidget->m_CurrentChannel2 = readout->values[2];

    newPos = m_Owner->mapToGlobal(newPos);

    m_ImageLensWidget->attachToOwner(m_Owner);

    if (!m_ImageLensWidget->isWindow() && m_ImageLensWidget->parentWidget())
    {
        newPos = m_ImageLensWidget->parentWidget()->mapFromGlobal(newPos);
    }

    m_ImageLensWidget->move(newPos);
    m_ImageLensWidget->update();
}

void ImagePreviewWidget::showImageLensWidget()
{
    if (m_MouseLeftButtonPressed)
    {
        m_ImageLensWidget->show();
        m_ImageLensWidget->raise();
    }
}

QRect ImagePreviewWidget::getScaledImageArea()
{
    QSize widgetSize = size();
    QSize pixmapSize = m_MainPixmap.size();

    // Calculate the aspect ratio for pixmap and widget
    double pixmapAspectRatio = static_cast<double>(pixmapSize.width()) / pixmapSize.height();
    double widgetAspectRatio = static_cast<double>(widgetSize.width()) / widgetSize.height();

    int targetWidth, targetHeight;
    if (pixmapAspectRatio > widgetAspectRatio)
    {
        // Pixmap is wider than widget
        targetWidth = widgetSize.width();
        targetHeight = static_cast<int>(targetWidth / pixmapAspectRatio);
    }
    else
    {
        // Pixmap is taller than widget or aspect ratios are equal
        targetHeight = widgetSize.height();
        targetWidth = static_cast<int>(targetHeight * pixmapAspectRatio);
    }

    // Calculate the top-left corner for centering the pixmap
    int x = (widgetSize.width() - targetWidth) / 2;
    int y = (widgetSize.height() - targetHeight) / 2;

    // Draw the pixmap centered and scaled
    QRect targetRect(x, y, targetWidth, targetHeight);

    return targetRect;
}

NoImagePreviewWidget::NoImagePreviewWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_DeleteOnClose);

    m_MainPixmap = loadNoImagePreviewPixmap();
    m_BackgroundColor = an::ui::tokens().colors.bgPrimary;
}

void NoImagePreviewWidget::setPixmap(const QPixmap& pixmap, bool scaled)
{
    m_MainPixmap = pixmap;
    m_DrawScaled = scaled;
    update();
}

void NoImagePreviewWidget::setBackgroundColor(const QColor& color)
{
    m_BackgroundColor = color;
    update();
}

QColor NoImagePreviewWidget::getBackgroundColor() const
{
    return m_BackgroundColor;
}

void NoImagePreviewWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
    {
        m_BackgroundColor = an::ui::tokens().colors.bgPrimary;
        update();
    }
    QWidget::changeEvent(ev);
}

void NoImagePreviewWidget::paintEvent(QPaintEvent* evt)
{
    if (m_MainPixmap.isNull())
    {
        return;
    }

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(this->rect(), m_BackgroundColor);

    if (m_DrawScaled)
    {
        auto targetRect = getScaledImageArea();

        painter.drawPixmap(targetRect, m_MainPixmap);
    }
    else
    {
        QSize widgetSize = size();
        QSize pixmapSize = m_MainPixmap.size();

        // Calculate the top-left corner for centering the pixmap
        int x = (widgetSize.width() - pixmapSize.width()) / 2;
        int y = (widgetSize.height() - pixmapSize.height()) / 2;

        // Draw the pixmap centered and without scaling
        painter.drawPixmap(x, y, m_MainPixmap);
    }
}

QRect NoImagePreviewWidget::getScaledImageArea()
{
    QSize widgetSize = size();
    QSize pixmapSize = m_MainPixmap.size();

    // Calculate the aspect ratio for pixmap and widget
    double pixmapAspectRatio = static_cast<double>(pixmapSize.width()) / pixmapSize.height();
    double widgetAspectRatio = static_cast<double>(widgetSize.width()) / widgetSize.height();

    int targetWidth, targetHeight;
    if (pixmapAspectRatio > widgetAspectRatio)
    {
        // Pixmap is wider than widget
        targetWidth = widgetSize.width();
        targetHeight = static_cast<int>(targetWidth / pixmapAspectRatio);
    }
    else
    {
        // Pixmap is taller than widget or aspect ratios are equal
        targetHeight = widgetSize.height();
        targetWidth = static_cast<int>(targetHeight * pixmapAspectRatio);
    }

    // Calculate the top-left corner for centering the pixmap
    int x = (widgetSize.width() - targetWidth) / 2;
    int y = (widgetSize.height() - targetHeight) / 2;

    // Draw the pixmap centered and scaled
    QRect targetRect(x, y, targetWidth, targetHeight);

    return targetRect;
}

NoImagePreviewWidget::~NoImagePreviewWidget() {}
