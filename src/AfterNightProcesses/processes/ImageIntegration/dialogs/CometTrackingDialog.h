// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "CometTrackingPreviewController.h"
#include "components/AnCollapsibleSection.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/TooltipFormatUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QPen>
#include <QResizeEvent>
#include <QPushButton>
#include <QPointer>
#include <QSizeF>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedLayout>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace an::ui
{
/// The size of the image a view shows, in its own pixels, or nothing while it shows none.
[[nodiscard]] inline std::optional<QSize> cometImageViewSourceSize(const ImageViewControl* control)
{
    const std::optional<an::ImageDescriptor> descriptor =
        control != nullptr ? control->imageDescriptor() : std::optional<an::ImageDescriptor>{};
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    return QSize(static_cast<int>(descriptor->extent().width()), static_cast<int>(descriptor->extent().height()));
}

struct CometTrackingDialogDiagnostic
{
    QString candidateId;
    QString frameId;
    QPointF sourcePosition;
    double confidence = 0.0;
    QString state;
    QString message;
};

using CometTrackingAutoTrackCompletion = std::function<void(std::vector<CometTrackingDialogDiagnostic>, QString)>;
using CometTrackingAutoTrackHandler =
    std::function<void(const RegistrationSettings::CometTrackingSettings&, CometTrackingAutoTrackCompletion)>;
using CometTrackingBlindDetectHandler =
    std::function<void(const RegistrationSettings::CometTrackingSettings&, CometTrackingAutoTrackCompletion)>;
using CometTrackingFramePreviewSizeProvider = std::function<std::optional<QSize>(const QString&)>;

inline void setCometPendingPointProperties(QWidget* widget, const std::optional<QPointF>& sourcePoint)
{
    if (widget == nullptr)
    {
        return;
    }

    widget->setProperty("anMarkerVisible", sourcePoint.has_value());
    widget->setProperty("anMarkerSourceX", sourcePoint.has_value() ? QVariant(sourcePoint->x()) : QVariant());
    widget->setProperty("anMarkerSourceY", sourcePoint.has_value() ? QVariant(sourcePoint->y()) : QVariant());
}

inline void paintCometPendingPointMarker(QPainter& painter, const QPointF& widgetPoint)
{
    const auto& t = tokens();
    const int radius = t.spacing.md;
    const int gap = t.spacing.xs;
    const int extent = radius + gap;

    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen halo_pen(t.colors.bgPrimary);
    halo_pen.setWidth((std::max)(t.spacing.xs, 1));
    painter.setPen(halo_pen);
    painter.drawEllipse(widgetPoint, radius, radius);
    painter.drawLine(QPointF(widgetPoint.x() - extent, widgetPoint.y()),
                     QPointF(widgetPoint.x() - gap, widgetPoint.y()));
    painter.drawLine(QPointF(widgetPoint.x() + gap, widgetPoint.y()),
                     QPointF(widgetPoint.x() + extent, widgetPoint.y()));
    painter.drawLine(QPointF(widgetPoint.x(), widgetPoint.y() - extent),
                     QPointF(widgetPoint.x(), widgetPoint.y() - gap));
    painter.drawLine(QPointF(widgetPoint.x(), widgetPoint.y() + gap),
                     QPointF(widgetPoint.x(), widgetPoint.y() + extent));

    QPen marker_pen(t.colors.accentPrimary);
    marker_pen.setWidth((std::max)(t.spacing.xs / 2, 1));
    painter.setPen(marker_pen);
    painter.drawEllipse(widgetPoint, radius, radius);
    painter.drawLine(QPointF(widgetPoint.x() - extent, widgetPoint.y()),
                     QPointF(widgetPoint.x() - gap, widgetPoint.y()));
    painter.drawLine(QPointF(widgetPoint.x() + gap, widgetPoint.y()),
                     QPointF(widgetPoint.x() + extent, widgetPoint.y()));
    painter.drawLine(QPointF(widgetPoint.x(), widgetPoint.y() - extent),
                     QPointF(widgetPoint.x(), widgetPoint.y() - gap));
    painter.drawLine(QPointF(widgetPoint.x(), widgetPoint.y() + gap),
                     QPointF(widgetPoint.x(), widgetPoint.y() + extent));
}

[[nodiscard]] inline QPointF mapCometPreviewSourcePixelToWidgetPoint(const QPointF& sourcePoint,
                                                                     const CometTrackingPreviewGeometry& geometry)
{
    return QPointF(geometry.imageTopLeft.x() + sourcePoint.x() * geometry.scale,
                   geometry.imageTopLeft.y() + sourcePoint.y() * geometry.scale);
}

class CometTrackingFallbackPreviewPane final : public QLabel
{
public:
    explicit CometTrackingFallbackPreviewPane(QWidget* parent = nullptr) : QLabel(parent)
    {
        setText(tr("No preview"));
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", false);
    }

    void setSourceSize(std::optional<QSize> sourceSize)
    {
        m_sourceSize = std::move(sourceSize);
        m_previewImage = QImage();
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", false);
        setText(m_sourceSize.has_value() ? tr("Preview unavailable") : tr("No preview"));
        update();
    }

    void setLoading(std::optional<QSize> sourceSize)
    {
        m_sourceSize = std::move(sourceSize);
        m_previewImage = QImage();
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", true);
        setText(tr("Loading preview..."));
        update();
    }

    void setPreviewUnavailable(const QString& message, std::optional<QSize> sourceSize = std::nullopt)
    {
        m_sourceSize = std::move(sourceSize);
        m_previewImage = QImage();
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", false);
        setText(message.trimmed().isEmpty() ? tr("Preview unavailable") : message);
        update();
    }

    void setPreviewImage(const QImage& image, const QSize& sourceSize)
    {
        m_sourceSize = sourceSize.isValid() && !sourceSize.isEmpty() ? std::optional<QSize>(sourceSize) : std::nullopt;
        m_previewImage = image;
        setProperty("anPreviewLoaded", !m_previewImage.isNull());
        setProperty("anPreviewLoading", false);
        if (m_previewImage.isNull())
        {
            setText(tr("Preview unavailable"));
            update();
            return;
        }

        setText(QString());
        update();
    }

    void setSourcePointSelectedHandler(std::function<void(QPointF)> handler)
    {
        m_sourcePointSelected = std::move(handler);
    }

    void setPendingSourcePoint(std::optional<QPointF> sourcePoint)
    {
        m_pendingSourcePoint = std::move(sourcePoint);
        setCometPendingPointProperties(this, m_pendingSourcePoint);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event != nullptr && event->button() == Qt::LeftButton && m_sourcePointSelected)
        {
            if (const std::optional<CometTrackingPreviewGeometry> geometry = currentGeometry())
            {
                if (const std::optional<QPointF> source_point =
                        mapCometPreviewWidgetPointToSourcePixel(event->position(), *geometry))
                {
                    m_sourcePointSelected(*source_point);
                }
            }
        }

        QLabel::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QLabel::paintEvent(event);

        const std::optional<CometTrackingPreviewGeometry> geometry = currentGeometry();
        if (!geometry.has_value())
        {
            return;
        }

        QPainter painter(this);
        if (!m_previewImage.isNull())
        {
            const QSizeF target_size(static_cast<double>(geometry->sourceSize.width()) * geometry->scale,
                                     static_cast<double>(geometry->sourceSize.height()) * geometry->scale);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(QRectF(geometry->imageTopLeft, target_size), m_previewImage);
        }

        if (m_pendingSourcePoint.has_value())
        {
            paintCometPendingPointMarker(painter,
                                         mapCometPreviewSourcePixelToWidgetPoint(*m_pendingSourcePoint, *geometry));
        }
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        update();
    }

private:
    [[nodiscard]] std::optional<CometTrackingPreviewGeometry> currentGeometry() const
    {
        if (!m_sourceSize.has_value() || m_sourceSize->width() <= 0 || m_sourceSize->height() <= 0 || width() <= 0 ||
            height() <= 0)
        {
            return std::nullopt;
        }

        const double scale = (std::min)(static_cast<double>(width()) / static_cast<double>(m_sourceSize->width()),
                                        static_cast<double>(height()) / static_cast<double>(m_sourceSize->height()));
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return std::nullopt;
        }

        const QSizeF scaled_size(static_cast<double>(m_sourceSize->width()) * scale,
                                 static_cast<double>(m_sourceSize->height()) * scale);
        return CometTrackingPreviewGeometry{*m_sourceSize, scale,
                                            QPointF((static_cast<double>(width()) - scaled_size.width()) * 0.5,
                                                    (static_cast<double>(height()) - scaled_size.height()) * 0.5)};
    }

    std::optional<QSize> m_sourceSize;
    std::optional<QPointF> m_pendingSourcePoint;
    QImage m_previewImage;
    std::function<void(QPointF)> m_sourcePointSelected;
};

class CometTrackingPendingPointOverlay final : public QWidget
{
public:
    explicit CometTrackingPendingPointOverlay(ImageViewControl* control, QWidget* parent)
        : QWidget(parent), m_control(control)
    {
        setObjectName(QStringLiteral("cometPendingPointOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setCometPendingPointProperties(this, m_sourcePoint);
        hide();

        if (parent != nullptr)
        {
            parent->installEventFilter(this);
            syncGeometryToParent();
        }
    }

    void setPendingSourcePoint(std::optional<QPointF> sourcePoint)
    {
        m_sourcePoint = std::move(sourcePoint);
        setCometPendingPointProperties(this, m_sourcePoint);
        setVisible(m_sourcePoint.has_value());
        syncGeometryToParent();
        update();
    }

    void refreshMarker()
    {
        syncGeometryToParent();
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == parentWidget() && event != nullptr &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Move))
        {
            syncGeometryToParent();
        }

        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        if (!m_sourcePoint.has_value() || m_control == nullptr)
        {
            return;
        }

        auto* view_widget = m_control->getViewWidget();
        const std::optional<QSize> image_size = cometImageViewSourceSize(m_control);
        if (view_widget == nullptr || !image_size.has_value())
        {
            return;
        }

        const double scale = view_widget->getCurrentScaleFactor();
        if (!std::isfinite(scale) || scale <= 0.0 || m_sourcePoint->x() < 0.0 || m_sourcePoint->y() < 0.0 ||
            m_sourcePoint->x() >= static_cast<double>(image_size->width()) ||
            m_sourcePoint->y() >= static_cast<double>(image_size->height()))
        {
            return;
        }

        QPainter painter(this);
        paintCometPendingPointMarker(painter, QPointF(m_sourcePoint->x() * scale, m_sourcePoint->y() * scale));
    }

private:
    void syncGeometryToParent()
    {
        if (QWidget* parent = parentWidget())
        {
            setGeometry(parent->rect());
            raise();
        }
    }

    ImageViewControl* m_control = nullptr;
    std::optional<QPointF> m_sourcePoint;
};

class CometTrackingImageViewClickFilter final : public QObject
{
public:
    CometTrackingImageViewClickFilter(ImageViewControl* control, std::function<void(QPointF)> callback,
                                      QObject* parent = nullptr)
        : QObject(parent), m_control(control), m_callback(std::move(callback))
    {
    }

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        if (event == nullptr || event->type() != QEvent::MouseButtonPress)
        {
            return false;
        }

        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() != Qt::LeftButton || m_control == nullptr || !m_callback)
        {
            return false;
        }

        if (const auto source_point = mapImageViewClickToCometSourcePixel(m_control, mouse_event->position()))
        {
            m_callback(*source_point);
        }

        return false;
    }

private:
    [[nodiscard]] static std::optional<QPointF> mapImageViewClickToCometSourcePixel(ImageViewControl* control,
                                                                                    const QPointF& viewPosition)
    {
        auto* view_widget = control != nullptr ? control->getViewWidget() : nullptr;
        const std::optional<QSize> image_size = cometImageViewSourceSize(control);
        if (view_widget == nullptr || !image_size.has_value())
        {
            return std::nullopt;
        }

        const double scale = view_widget->getCurrentScaleFactor();
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return std::nullopt;
        }

        const QPointF source_point(viewPosition.x() / scale, viewPosition.y() / scale);
        if (source_point.x() < 0.0 || source_point.y() < 0.0 ||
            source_point.x() >= static_cast<double>(image_size->width()) ||
            source_point.y() >= static_cast<double>(image_size->height()))
        {
            return std::nullopt;
        }

        return source_point;
    }

    ImageViewControl* m_control = nullptr;
    std::function<void(QPointF)> m_callback;
};

class CometTrackingImageViewLoadListener final : public ImageViewControlListener
{
public:
    using StateChangedHandler = std::function<void(ImageViewControl*, bool, bool, QString)>;

    CometTrackingImageViewLoadListener(ImageViewControl* control, StateChangedHandler stateChanged)
        : ImageViewControlListener(control), m_stateChanged(std::move(stateChanged))
    {
    }

    void onPreImageLoaded(ImageViewControl*) override
    {
        setStatus(nullptr, false, true);
    }

    void onImageLoaded(ImageViewControl* control) override
    {
        setStatus(control, control != nullptr && control->hasImage(), false);
    }

    void onImageLoadFailed(ImageViewControl* control, const QString& message) override
    {
        setStatus(control, false, false, message);
    }

    void onPreImageUpdated(ImageViewControl*) override
    {
        setStatus(nullptr, false, true);
    }

    void onImageUpdated(ImageViewControl* control) override
    {
        onImageLoaded(control);
    }

private:
    void setStatus(ImageViewControl* control, const bool loaded, const bool loading, const QString& message = {})
    {
        if (!m_stateChanged)
        {
            return;
        }

        m_stateChanged(control, loaded, loading, message);
    }

    StateChangedHandler m_stateChanged;
};

class CometTrackingPreviewPane final : public QWidget
{
public:
    explicit CometTrackingPreviewPane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        m_stack = new QStackedLayout(this);
        m_stack->setContentsMargins(0, 0, 0, 0);
        m_stack->setSpacing(0);

        m_fallbackPreview = new CometTrackingFallbackPreviewPane(this);
        m_fallbackPreview->setObjectName(QStringLiteral("cometPreviewFallbackLabel"));
        m_fallbackPreview->setAlignment(Qt::AlignCenter);
        m_fallbackPreview->setFrameShape(QFrame::StyledPanel);
        m_fallbackPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_stack->addWidget(m_fallbackPreview);

        if (auto* control = createImageViewPreviewControl(this))
        {
            m_imageViewControl = control;
            m_imageViewControl->setObjectName(QStringLiteral("cometImageViewControl"));
            m_imageViewControl->setAccessibleName(tr("Comet preview"));
            m_imageViewControl->setToolTip(
                tr("Preview the selected frame, zoom, and click the comet core to mark it."));
            m_imageViewControl->setAutoDebayerCfaImagePreviews(true);
            m_imageViewControl->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
            m_imageViewControl->setCalibrationPreviewVisible(false);
            m_imageViewControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_stack->addWidget(m_imageViewControl);

            if (auto* view_widget = m_imageViewControl->getViewWidget())
            {
                m_imageViewPendingOverlay = new CometTrackingPendingPointOverlay(m_imageViewControl, view_widget);
                view_widget->installEventFilter(new CometTrackingImageViewClickFilter(
                    m_imageViewControl, [this](QPointF source_point) { emitSourcePointSelected(source_point); },
                    view_widget));
            }
            m_imageViewListener = std::make_unique<CometTrackingImageViewLoadListener>(
                m_imageViewControl,
                [this](ImageViewControl* control, const bool loaded, const bool loading, const QString& message) {
                    handleImageViewPreviewStateChanged(control, loaded, loading, message);
                });
        }

        showFallbackPreview();
    }

    [[nodiscard]] bool usesImageViewControl() const
    {
        return m_imageViewControl != nullptr;
    }

    [[nodiscard]] ImageViewControl* imageViewControl() const
    {
        return m_imageViewControl;
    }

    void setImageViewLoading(std::optional<QSize> sourceSize)
    {
        if (m_imageViewControl == nullptr)
        {
            m_fallbackPreview->setLoading(std::move(sourceSize));
            syncFallbackProperties();
            return;
        }

        showImageViewPreview();
        setProperty("anPreviewImageWidth", 0);
        setProperty("anPreviewImageHeight", 0);
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", true);
        setProperty("anPreviewErrorMessage", QString());
    }

    void loadImageViewFile(const QString& framePath, std::optional<QSize> sourceSize)
    {
        if (m_imageViewControl == nullptr)
        {
            m_fallbackPreview->setLoading(std::move(sourceSize));
            syncFallbackProperties();
            return;
        }

        showImageViewPreview();
        setProperty("anPreviewImageWidth", 0);
        setProperty("anPreviewImageHeight", 0);
        setProperty("anPreviewLoaded", false);
        setProperty("anPreviewLoading", true);
        setProperty("anPreviewErrorMessage", QString());
        m_pendingImageViewState = captureImageViewState();
        ++m_imageViewStateRestoreGeneration;
        m_imageViewControl->loadImageFromFile(framePath.toStdString());
    }

    void setSourceSize(std::optional<QSize> sourceSize)
    {
        cancelImageViewStateRestore();
        showFallbackPreview();
        m_fallbackPreview->setSourceSize(std::move(sourceSize));
        syncFallbackProperties();
    }

    void setLoading(std::optional<QSize> sourceSize)
    {
        cancelImageViewStateRestore();
        showFallbackPreview();
        m_fallbackPreview->setLoading(std::move(sourceSize));
        syncFallbackProperties();
    }

    void setPreviewUnavailable(const QString& message, std::optional<QSize> sourceSize = std::nullopt)
    {
        cancelImageViewStateRestore();
        showFallbackPreview();
        if (m_imageViewControl != nullptr)
        {
            m_imageViewControl->clear();
        }
        m_fallbackPreview->setPreviewUnavailable(message, std::move(sourceSize));
        setProperty("anPreviewErrorMessage", message);
        syncFallbackProperties();
    }

    void setPreviewImage(const QImage& image, const QSize& sourceSize)
    {
        showFallbackPreview();
        m_fallbackPreview->setPreviewImage(image, sourceSize);
        setProperty("anPreviewImageWidth", image.width());
        setProperty("anPreviewImageHeight", image.height());
        syncFallbackProperties();
    }

    void setSourcePointSelectedHandler(std::function<void(QPointF)> handler)
    {
        m_sourcePointSelected = std::move(handler);
        if (m_fallbackPreview != nullptr)
        {
            m_fallbackPreview->setSourcePointSelectedHandler(
                [this](QPointF source_point) { emitSourcePointSelected(source_point); });
        }
    }

    void setPendingSourcePoint(std::optional<QPointF> sourcePoint)
    {
        m_pendingSourcePoint = std::move(sourcePoint);
        setCometPendingPointProperties(this, m_pendingSourcePoint);
        if (m_fallbackPreview != nullptr)
        {
            m_fallbackPreview->setPendingSourcePoint(m_pendingSourcePoint);
        }
        if (m_imageViewPendingOverlay != nullptr)
        {
            m_imageViewPendingOverlay->setPendingSourcePoint(m_pendingSourcePoint);
        }
    }

private:
    struct ImageViewState
    {
        double scale = 1.0;
        QPoint viewportOffset;
    };

    [[nodiscard]] static ImageViewControl* createImageViewPreviewControl(QWidget* parent)
    {
        if (appContext() == nullptr)
        {
            return nullptr;
        }

        auto* control = new ImageViewControl(parent);
        control->initialize("CometTrackingPreview", ViewControlMode::st_preview);
        return control;
    }

    void handleImageViewPreviewStateChanged(ImageViewControl* control, const bool loaded, const bool loading,
                                            const QString& message)
    {
        const QString trimmed_message = message.trimmed();
        if (!trimmed_message.isEmpty())
        {
            setPreviewUnavailable(trimmed_message);
            return;
        }

        if (loaded)
        {
            if (const std::optional<QSize> image_size = cometImageViewSourceSize(control))
            {
                setProperty("anPreviewImageWidth", image_size->width());
                setProperty("anPreviewImageHeight", image_size->height());
            }
            showImageViewPreview();
            if (m_imageViewPendingOverlay != nullptr)
            {
                m_imageViewPendingOverlay->refreshMarker();
            }
            queueImageViewStateRestore();
        }

        setProperty("anPreviewLoaded", loaded);
        setProperty("anPreviewLoading", loading);
        setProperty("anPreviewErrorMessage", QString());
    }

    void emitSourcePointSelected(const QPointF& sourcePoint)
    {
        if (m_sourcePointSelected)
        {
            m_sourcePointSelected(sourcePoint);
        }
    }

    [[nodiscard]] std::optional<ImageViewState> captureImageViewState() const
    {
        if (m_imageViewControl == nullptr || !m_imageViewControl->hasImage())
        {
            return std::nullopt;
        }

        auto* view_widget = m_imageViewControl->getViewWidget();
        if (view_widget == nullptr)
        {
            return std::nullopt;
        }

        const double scale = view_widget->getCurrentScaleFactor();
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return std::nullopt;
        }

        return ImageViewState{scale, m_imageViewControl->getViewportOffset()};
    }

    void queueImageViewStateRestore()
    {
        if (!m_pendingImageViewState.has_value())
        {
            return;
        }

        const quint64 restore_generation = m_imageViewStateRestoreGeneration;
        // Static preview loads apply fit-to-window after load notifications, so restore on the next event turn.
        QTimer::singleShot(0, this, [this, restore_generation]() {
            if (restore_generation != m_imageViewStateRestoreGeneration || !m_pendingImageViewState.has_value() ||
                m_imageViewControl == nullptr || !m_imageViewControl->hasImage())
            {
                return;
            }

            auto* view_widget = m_imageViewControl->getViewWidget();
            if (view_widget == nullptr)
            {
                return;
            }

            const ImageViewState state = *m_pendingImageViewState;
            m_pendingImageViewState.reset();
            view_widget->scaleImageView(state.scale, true);
            m_imageViewControl->setViewportOffset(state.viewportOffset);
            if (m_imageViewPendingOverlay != nullptr)
            {
                m_imageViewPendingOverlay->refreshMarker();
            }
        });
    }

    void cancelImageViewStateRestore()
    {
        m_pendingImageViewState.reset();
        ++m_imageViewStateRestoreGeneration;
    }

    void showFallbackPreview()
    {
        if (m_stack != nullptr && m_fallbackPreview != nullptr)
        {
            m_stack->setCurrentWidget(m_fallbackPreview);
        }
    }

    void showImageViewPreview()
    {
        if (m_stack != nullptr && m_imageViewControl != nullptr)
        {
            m_stack->setCurrentWidget(m_imageViewControl);
        }
    }

    void syncFallbackProperties()
    {
        if (m_fallbackPreview == nullptr)
        {
            return;
        }

        setProperty("anPreviewLoaded", m_fallbackPreview->property("anPreviewLoaded"));
        setProperty("anPreviewLoading", m_fallbackPreview->property("anPreviewLoading"));
    }

    QStackedLayout* m_stack = nullptr;
    CometTrackingFallbackPreviewPane* m_fallbackPreview = nullptr;
    ImageViewControl* m_imageViewControl = nullptr;
    CometTrackingPendingPointOverlay* m_imageViewPendingOverlay = nullptr;
    std::unique_ptr<CometTrackingImageViewLoadListener> m_imageViewListener;
    std::optional<ImageViewState> m_pendingImageViewState;
    quint64 m_imageViewStateRestoreGeneration = 0;
    std::optional<QPointF> m_pendingSourcePoint;
    std::function<void(QPointF)> m_sourcePointSelected;
};

class CometTrackingDialog : public QDialog
{
public:
    explicit CometTrackingDialog(const QStringList& availableLightFrames, QWidget* parent = nullptr)
        : QDialog(parent), m_availableLightFrames(availableLightFrames)
    {
        setObjectName(QStringLiteral("cometTrackingDialog"));
        setWindowTitle(tr("Track Comet"));
        setWindowModality(Qt::WindowModal);
        setModal(true);
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setWindowFlag(Qt::WindowMaximizeButtonHint, true);
        setSizeGripEnabled(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        const auto& t = tokens();
        setMinimumSize(t.spacing.xl * 32, t.spacing.xl * 22);

        auto* root_layout = new QVBoxLayout(this);
        root_layout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        root_layout->setSpacing(t.spacing.md);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(QStringLiteral("cometTrackingStatusLabel"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        root_layout->addWidget(m_statusLabel);

        auto* content_splitter = new QSplitter(Qt::Vertical, this);
        content_splitter->setObjectName(QStringLiteral("cometTrackingMainSplitter"));
        content_splitter->setChildrenCollapsible(false);
        root_layout->addWidget(content_splitter, 1);

        auto* frame_preview_splitter = new QSplitter(Qt::Horizontal, content_splitter);
        frame_preview_splitter->setObjectName(QStringLiteral("cometFramePreviewSplitter"));
        frame_preview_splitter->setChildrenCollapsible(false);

        m_frameTable = new QTableWidget(this);
        m_frameTable->setObjectName(QStringLiteral("cometFrameTable"));
        m_frameTable->setColumnCount(1);
        m_frameTable->setHorizontalHeaderLabels({tr("Frame")});
        m_frameTable->horizontalHeader()->setStretchLastSection(true);
        m_frameTable->verticalHeader()->setVisible(false);
        m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_frameTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        populateFrameTable();
        frame_preview_splitter->addWidget(m_frameTable);

        m_previewPlaceholder = new CometTrackingPreviewPane(this);
        m_previewPlaceholder->setObjectName(QStringLiteral("cometPreviewPlaceholderLabel"));
        m_previewPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_previewPlaceholder->setSourcePointSelectedHandler(
            [this](QPointF source_position) { setPendingSourceFramePosition(source_position); });
        frame_preview_splitter->addWidget(m_previewPlaceholder);
        frame_preview_splitter->setStretchFactor(0, 1);
        frame_preview_splitter->setStretchFactor(1, 3);
        frame_preview_splitter->setSizes({t.spacing.xl * 12, t.spacing.xl * 36});
        content_splitter->addWidget(frame_preview_splitter);

        m_observationTable = new QTableWidget(this);
        m_observationTable->setObjectName(QStringLiteral("cometObservationTable"));
        m_observationTable->setColumnCount(5);
        m_observationTable->setHorizontalHeaderLabels({tr("Use"), tr("Frame"), tr("X"), tr("Y"), tr("Origin")});
        m_observationTable->horizontalHeader()->setStretchLastSection(true);
        m_observationTable->verticalHeader()->setVisible(false);
        m_observationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_observationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        content_splitter->addWidget(m_observationTable);

        m_diagnosticsTable = new QTableWidget(this);
        m_diagnosticsTable->setObjectName(QStringLiteral("cometDiagnosticsTable"));
        m_diagnosticsTable->setColumnCount(5);
        m_diagnosticsTable->setHorizontalHeaderLabels({tr("Frame"), tr("X"), tr("Y"), tr("Confidence"), tr("State")});
        m_diagnosticsTable->horizontalHeader()->setStretchLastSection(true);
        m_diagnosticsTable->verticalHeader()->setVisible(false);
        m_diagnosticsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_diagnosticsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        content_splitter->addWidget(m_diagnosticsTable);
        content_splitter->setStretchFactor(0, 4);
        content_splitter->setStretchFactor(1, 1);
        content_splitter->setStretchFactor(2, 1);
        content_splitter->setSizes({t.spacing.xl * 30, t.spacing.xl * 8, t.spacing.xl * 8});

        root_layout->addWidget(makeOptionsSection());

        auto* action_row = new QHBoxLayout();
        action_row->setSpacing(t.spacing.sm);
        m_markButton = new QPushButton(tr("Mark"), this);
        m_markButton->setObjectName(QStringLiteral("markCometButton"));
        m_clearPointButton = new QPushButton(tr("Clear Point"), this);
        m_clearPointButton->setObjectName(QStringLiteral("clearCometPointButton"));
        m_clearPointButton->setEnabled(false);
        m_refineButton = new QPushButton(tr("Refine"), this);
        m_refineButton->setObjectName(QStringLiteral("refineCometButton"));
        m_refineButton->setEnabled(false);
        m_refineButton->setToolTip(tr("Refine the pending comet core position on the selected frame."));
        m_autoTrackButton = new QPushButton(tr("Auto Track"), this);
        m_autoTrackButton->setObjectName(QStringLiteral("autoTrackCometButton"));
        m_autoTrackButton->setToolTip(
            tr("Guided tracking starts from accepted observations. A production fit normally needs two accepted "
               "observations with valid timestamps."));
        m_findMovingObjectButton = new QPushButton(tr("Find Moving Object"), this);
        m_findMovingObjectButton->setObjectName(QStringLiteral("findMovingCometButton"));
        m_findMovingObjectButton->setToolTip(
            tr("Search for moving residual tracks. Candidate tracks stay diagnostics-only until you accept one."));
        m_clearButton = new QPushButton(tr("Clear"), this);
        m_clearButton->setObjectName(QStringLiteral("clearCometButton"));
        m_acceptButton = new QPushButton(tr("Accept"), this);
        m_acceptButton->setObjectName(QStringLiteral("acceptCometTrackingButton"));
        action_row->addWidget(m_markButton);
        action_row->addWidget(m_clearPointButton);
        action_row->addWidget(m_refineButton);
        action_row->addWidget(m_autoTrackButton);
        action_row->addWidget(m_findMovingObjectButton);
        action_row->addWidget(m_clearButton);
        action_row->addStretch(1);
        action_row->addWidget(m_acceptButton);
        root_layout->addLayout(action_row);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { attemptAcceptProductionFit(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root_layout->addWidget(buttons);

        connect(m_markButton, &QPushButton::clicked, this, [this]() { markCurrentFrameObservation(); });
        connect(m_clearPointButton, &QPushButton::clicked, this, [this]() { clearPendingSourceFramePosition(); });
        connect(m_acceptButton, &QPushButton::clicked, this, [this]() { acceptSelectedDiagnosticCandidate(); });
        connect(m_autoTrackButton, &QPushButton::clicked, this, [this]() { startAutoTracking(); });
        connect(m_findMovingObjectButton, &QPushButton::clicked, this, [this]() { startBlindDetection(); });
        connect(m_clearButton, &QPushButton::clicked, this, [this]() {
            m_settings.observations.clear();
            refreshObservationTable();
        });
        connect(m_observationTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { refreshStatus(); });
        connect(m_frameTable, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
            clearPendingSourceFramePosition();
            updatePreviewForSelectedFrame();
            refreshStatus();
        });
        connect(m_diagnosticsTable, &QTableWidget::itemSelectionChanged, this, [this]() { refreshStatus(); });

        setCometTrackingSettings(RegistrationSettings::CometTrackingSettings{});
        if (m_frameTable->rowCount() > 0)
        {
            m_frameTable->setCurrentCell(0, 0);
            m_frameTable->selectRow(0);
            updatePreviewForSelectedFrame();
        }
    }

    void setCometTrackingSettings(const RegistrationSettings::CometTrackingSettings& settings)
    {
        m_settings = settings;
        refreshOptionControls();
        refreshObservationTable();
    }

    void setPendingSourceFramePosition(QPointF sourcePosition)
    {
        m_pendingSourceFramePosition = sourcePosition;
        if (m_previewPlaceholder != nullptr)
        {
            m_previewPlaceholder->setPendingSourcePoint(m_pendingSourceFramePosition);
        }
        refreshStatus();
    }

    void clearPendingSourceFramePosition()
    {
        m_pendingSourceFramePosition.reset();
        if (m_previewPlaceholder != nullptr)
        {
            m_previewPlaceholder->setPendingSourcePoint(std::nullopt);
        }
        refreshStatus();
    }

    [[nodiscard]] RegistrationSettings::CometTrackingSettings cometTrackingSettings() const
    {
        RegistrationSettings::CometTrackingSettings settings = m_settings;
        settings.trackingOptions = shownTrackingOptions();
        settings.fitOptions = shownFitOptions();
        settings.observations.clear();
        settings.observations.reserve(m_observationTable->rowCount());

        for (int row = 0; row < m_observationTable->rowCount(); ++row)
        {
            const QTableWidgetItem* enabled_item = m_observationTable->item(row, 0);
            const QTableWidgetItem* frame_item = m_observationTable->item(row, 1);
            const QTableWidgetItem* x_item = m_observationTable->item(row, 2);
            const QTableWidgetItem* y_item = m_observationTable->item(row, 3);
            const QTableWidgetItem* origin_item = m_observationTable->item(row, 4);
            if (frame_item == nullptr || x_item == nullptr || y_item == nullptr || origin_item == nullptr)
            {
                continue;
            }

            RegistrationSettings::CometTrackingObservation observation;
            observation.enabled = enabled_item == nullptr || enabled_item->checkState() == Qt::Checked;
            observation.frameId = frame_item->data(Qt::UserRole).toString();
            observation.sourcePosition =
                QPointF(x_item->data(Qt::UserRole).toDouble(), y_item->data(Qt::UserRole).toDouble());
            bool origin_ok = false;
            const int origin_value = origin_item->data(Qt::UserRole).toInt(&origin_ok);
            observation.origin = origin_ok ? static_cast<RegistrationSettings::CometObservationOrigin>(origin_value)
                                           : RegistrationSettings::CometObservationOrigin::Manual;
            settings.observations.push_back(observation);
        }

        return settings;
    }

    /// Stops a preview read still on its way.
    ~CometTrackingDialog() override
    {
        m_previewStop.request_stop();
    }

    void setAutoTrackHandler(CometTrackingAutoTrackHandler handler)
    {
        m_autoTrackHandler = std::move(handler);
        refreshStatus();
    }

    void setBlindDetectHandler(CometTrackingBlindDetectHandler handler)
    {
        m_blindDetectHandler = std::move(handler);
        refreshStatus();
    }

    void setFramePreviewSizeProvider(CometTrackingFramePreviewSizeProvider provider)
    {
        m_framePreviewSizeProvider = std::move(provider);
        updatePreviewForSelectedFrame();
    }

    void setDiagnostics(std::vector<CometTrackingDialogDiagnostic> diagnostics)
    {
        m_diagnostics = std::move(diagnostics);
        refreshDiagnosticsTable();
        refreshStatus();
    }

    [[nodiscard]] bool canAcceptProductionFit() const
    {
        const RegistrationSettings::CometTrackingSettings settings = cometTrackingSettings();
        return std::count_if(settings.observations.cbegin(), settings.observations.cend(),
                             [](const RegistrationSettings::CometTrackingObservation& observation) {
                                 return observation.enabled;
                             }) >= 2;
    }

private:
    /// The tracking and fit options, in a section of their own that starts folded: how the nucleus is searched for,
    /// which Auto Track and the registration both measure with, and how the track is fitted.
    [[nodiscard]] AnCollapsibleSection* makeOptionsSection()
    {
        using imageintegration::formatTooltip;
        using imageintegration::tooltipBullets;
        using ImageIntegration::RegistrationSettingsUtils::kLargestCometSearchRadius;
        using ImageIntegration::RegistrationSettingsUtils::kSmallestCometOutlierSigma;
        const auto& t = tokens();
        const RegistrationSettings::CometTrackingSettings defaults;
        const an::CometTrackRequest track;

        auto* section = new AnCollapsibleSection(tr("Tracking and Fit Options"), this);
        section->setObjectName(QStringLiteral("cometTrackingOptionsSection"));
        auto* layout = new QGridLayout();
        layout->setHorizontalSpacing(t.spacing.md);
        layout->setVerticalSpacing(t.spacing.sm);
        section->setContentLayout(layout);
        // A control and its label share its tooltip.
        const auto place = [&](const int row, const int column, const QString& label_text, QWidget* control,
                               const QString& tooltip) {
            auto* label = new QLabel(label_text, section);
            label->setBuddy(control);
            label->setToolTip(tooltip);
            control->setToolTip(tooltip);
            layout->addWidget(label, row, column);
            layout->addWidget(control, row, column + 1);
        };

        m_searchRadiusSpinBox = new QSpinBox(section);
        m_searchRadiusSpinBox->setObjectName(QStringLiteral("cometSearchRadiusSpinBox"));
        m_searchRadiusSpinBox->setRange(1, kLargestCometSearchRadius);
        m_searchRadiusSpinBox->setSuffix(tr(" px"));
        place(0, 0, tr("Search radius:"), m_searchRadiusSpinBox,
              formatTooltip(
                  tr("Nucleus search radius"),
                  {tr("What it does:\nThe nucleus is measured within this many pixels of where the track places it "
                      "in each frame, or of the marked position when Auto Track searches from a single observation. "
                      "Auto Track and the registration both measure with it."),
                   tr("Adjust it when:\n%1")
                       .arg(tooltipBullets(
                           {tr("Raise it when frames stay predicted-only because the nucleus lies further from where "
                               "the track places it."),
                            tr("Lower it in a crowded field, so that a star beside the nucleus is not measured "
                               "instead.")})),
                   tr("Range: 1 to %1 px, the largest region the nucleus measurement takes. Default: %2 px.")
                       .arg(kLargestCometSearchRadius)
                       .arg(defaults.trackingOptions.searchRadiusPixels)}));

        m_minimumSnrSpinBox = new QDoubleSpinBox(section);
        m_minimumSnrSpinBox->setObjectName(QStringLiteral("cometMinimumSnrSpinBox"));
        m_minimumSnrSpinBox->setRange(0.1, 100.0);
        m_minimumSnrSpinBox->setDecimals(1);
        m_minimumSnrSpinBox->setSingleStep(0.5);
        place(0, 2, tr("Minimum SNR:"), m_minimumSnrSpinBox,
              formatTooltip(tr("Minimum signal-to-noise ratio"),
                            {tr("What it does:\nA nucleus whose peak is less than this many times the noise around it "
                                "is not taken: the frame is aligned where the track places it, and Auto Track lists "
                                "it as predicted-only."),
                             tr("Adjust it when:\n%1")
                                 .arg(tooltipBullets(
                                     {tr("Lower it for a faint or diffuse comet whose frames stay predicted-only."),
                                      tr("Raise it when noise or a passing star is taken for the nucleus.")})),
                             tr("Default: %1").arg(defaults.trackingOptions.minimumSnr, 0, 'f', 1)}));

        m_bootstrapFramesSpinBox = new QSpinBox(section);
        m_bootstrapFramesSpinBox->setObjectName(QStringLiteral("cometBootstrapFramesSpinBox"));
        // No search from one observation could propose more candidates than one track fit takes observations.
        m_bootstrapFramesSpinBox->setRange(0, static_cast<int>(an::kMaximumCometTrackObservations));
        place(0, 4, tr("Bootstrap frames:"), m_bootstrapFramesSpinBox,
              formatTooltip(tr("Bootstrap frames"),
                            {tr("What it does:\nWith one accepted observation, Auto Track searches at most this many "
                                "other frames near the marked position and proposes what it finds, which is kept "
                                "only once accepted. With two or more observations the track is fitted instead and "
                                "this has no effect."),
                             tr("Default: %1").arg(defaults.trackingOptions.maxBootstrapFrames)}));

        m_trackModelComboBox = new QComboBox(section);
        m_trackModelComboBox->setObjectName(QStringLiteral("cometTrackModelComboBox"));
        m_trackModelComboBox->addItem(tr("Linear"), QStringLiteral("linear"));
        m_trackModelComboBox->addItem(tr("Automatic"), QStringLiteral("automatic"));
        m_trackModelComboBox->addItem(tr("Prefer quadratic"), QStringLiteral("quadratic"));
        place(1, 0, tr("Track model:"), m_trackModelComboBox,
              formatTooltip(
                  tr("Track model"),
                  {tr("What it does:\nHow the comet's path across the frames is fitted to the accepted "
                      "observations."),
                   tr("Options:\n%1")
                       .arg(tooltipBullets(
                           {tr("Linear: a constant velocity, right for most sessions."),
                            tr("Automatic: an acceleration only when %1 or more observations remain and it lowers "
                               "the track's residual by %2% or more.")
                                .arg(track.minimumQuadraticObservations)
                                .arg(track.minimumQuadraticImprovement * 100.0),
                            tr("Prefer quadratic: an acceleration whenever %1 or more observations remain.")
                                .arg(track.minimumQuadraticObservations)})),
                   tr("Default: Linear")}));

        m_robustRejectionCheckBox = new QCheckBox(tr("Reject outlier observations"), section);
        m_robustRejectionCheckBox->setObjectName(QStringLiteral("cometRobustRejectionCheckBox"));
        m_robustRejectionCheckBox->setToolTip(formatTooltip(
            tr("Reject outlier observations"),
            {tr("What it does:\nLeaves out of the fit an observation further from the track than Outlier sigma "
                "times the residual of the fit without it, when leaving it out at least halves the fit's residual. "
                "An observation within %1 px of the track is never left out.")
                 .arg(track.minimumOutlierResidualPixels),
             tr("Default: on")}));
        layout->addWidget(m_robustRejectionCheckBox, 1, 2, 1, 2);

        m_outlierSigmaSpinBox = new QDoubleSpinBox(section);
        m_outlierSigmaSpinBox->setObjectName(QStringLiteral("cometOutlierSigmaSpinBox"));
        m_outlierSigmaSpinBox->setRange(kSmallestCometOutlierSigma, 100.0);
        m_outlierSigmaSpinBox->setDecimals(1);
        m_outlierSigmaSpinBox->setSingleStep(0.5);
        place(1, 4, tr("Outlier sigma:"), m_outlierSigmaSpinBox,
              formatTooltip(tr("Outlier sigma"),
                            {tr("What it does:\nThe multiple of the fit's residual beyond which an observation may be "
                                "left out, while outlier observations are rejected."),
                             tr("Adjust it when:\n%1")
                                 .arg(tooltipBullets({tr("Lower it to leave out a misplaced mark more readily."),
                                                      tr("Raise it to keep every observation but a gross error.")})),
                             tr("Default: %1").arg(defaults.fitOptions.outlierSigma, 0, 'f', 1)}));
        connect(m_robustRejectionCheckBox, &QCheckBox::toggled, m_outlierSigmaSpinBox, &QWidget::setEnabled);
        layout->setColumnStretch(6, 1);
        return section;
    }

    /// Shows the stored options in their controls.
    void refreshOptionControls()
    {
        if (m_searchRadiusSpinBox == nullptr)
        {
            return;
        }
        using ImageIntegration::RegistrationSettingsUtils::cometFitModelKey;
        using ImageIntegration::RegistrationSettingsUtils::effectiveCometSearchRadius;
        const RegistrationSettings::CometTrackingOptions& tracking = m_settings.trackingOptions;
        const RegistrationSettings::CometFitOptions& fit = m_settings.fitOptions;
        m_searchRadiusSpinBox->setValue(effectiveCometSearchRadius(tracking.searchRadiusPixels));
        m_minimumSnrSpinBox->setValue(tracking.minimumSnr);
        m_bootstrapFramesSpinBox->setValue(tracking.maxBootstrapFrames);
        m_trackModelComboBox->setCurrentIndex(
            (std::max)(m_trackModelComboBox->findData(cometFitModelKey(fit.model)), 0));
        m_robustRejectionCheckBox->setChecked(fit.robustRejection);
        m_outlierSigmaSpinBox->setValue(fit.outlierSigma);
        m_outlierSigmaSpinBox->setEnabled(fit.robustRejection);
    }

    /// A stored value while its control shows the value it was stored as, rounded to the control's decimals,
    /// otherwise the control's: a value stored more finely than a control shows - a file edited by hand - is not
    /// rounded, and the settings and their fingerprints not changed, by opening and accepting the dialog.
    [[nodiscard]] static double storedWhileShown(const QDoubleSpinBox& control, const double stored)
    {
        return std::isfinite(stored) && QString::number(stored, 'f', control.decimals()).toDouble() == control.value()
                   ? stored
                   : control.value();
    }

    /// The tracking options the controls show, each stored one kept while its control shows what it runs as.
    [[nodiscard]] RegistrationSettings::CometTrackingOptions shownTrackingOptions() const
    {
        const RegistrationSettings::CometTrackingOptions& stored = m_settings.trackingOptions;
        if (m_searchRadiusSpinBox == nullptr)
        {
            return stored;
        }
        RegistrationSettings::CometTrackingOptions shown = stored;
        const int radius = m_searchRadiusSpinBox->value();
        if (ImageIntegration::RegistrationSettingsUtils::effectiveCometSearchRadius(stored.searchRadiusPixels) !=
            radius)
        {
            shown.searchRadiusPixels = radius;
        }
        shown.minimumSnr = storedWhileShown(*m_minimumSnrSpinBox, stored.minimumSnr);
        shown.maxBootstrapFrames = m_bootstrapFramesSpinBox->value();
        return shown;
    }

    /// The fit options the controls show, a stored spelling of the track model kept while the model it names is shown.
    [[nodiscard]] RegistrationSettings::CometFitOptions shownFitOptions() const
    {
        const RegistrationSettings::CometFitOptions& stored = m_settings.fitOptions;
        if (m_trackModelComboBox == nullptr)
        {
            return stored;
        }
        RegistrationSettings::CometFitOptions shown = stored;
        const QString model = m_trackModelComboBox->currentData().toString();
        if (ImageIntegration::RegistrationSettingsUtils::cometFitModelKey(stored.model) != model)
        {
            shown.model = model;
        }
        shown.robustRejection = m_robustRejectionCheckBox->isChecked();
        shown.outlierSigma = storedWhileShown(*m_outlierSigmaSpinBox, stored.outlierSigma);
        return shown;
    }

    [[nodiscard]] int acceptedObservationCount() const
    {
        const RegistrationSettings::CometTrackingSettings settings = cometTrackingSettings();
        return static_cast<int>(std::count_if(
            settings.observations.cbegin(), settings.observations.cend(),
            [](const RegistrationSettings::CometTrackingObservation& observation) { return observation.enabled; }));
    }

    [[nodiscard]] static QString frameLabel(const QString& frame_id)
    {
        const QString label = QFileInfo(frame_id).fileName();
        return label.isEmpty() ? frame_id : label;
    }

    [[nodiscard]] static bool sameFrameId(const QString& lhs, const QString& rhs)
    {
        if (lhs.trimmed().isEmpty() || rhs.trimmed().isEmpty())
        {
            return false;
        }

        try
        {
            return FrameId(lhs) == FrameId(rhs);
        }
        catch (...)
        {
            return lhs.trimmed() == rhs.trimmed();
        }
    }

    [[nodiscard]] static QString originLabel(RegistrationSettings::CometObservationOrigin origin)
    {
        switch (origin)
        {
        case RegistrationSettings::CometObservationOrigin::Manual:
            return QCoreApplication::translate("CometTrackingDialog", "Manual");
        case RegistrationSettings::CometObservationOrigin::AcceptedAutomatic:
            return QCoreApplication::translate("CometTrackingDialog", "Accepted");
        case RegistrationSettings::CometObservationOrigin::Imported:
            return QCoreApplication::translate("CometTrackingDialog", "Imported");
        }
        return QCoreApplication::translate("CometTrackingDialog", "Manual");
    }

    [[nodiscard]] QString selectedFrameId() const
    {
        const int row = selectedFrameRow();
        if (row < 0)
        {
            return {};
        }

        const QTableWidgetItem* item = m_frameTable->item(row, 0);
        return item != nullptr ? item->data(Qt::UserRole).toString() : QString{};
    }

    [[nodiscard]] int selectedFrameRow() const
    {
        if (m_frameTable == nullptr)
        {
            return -1;
        }

        const QModelIndex current_index = m_frameTable->currentIndex();
        if (current_index.isValid() && current_index.row() >= 0 && current_index.row() < m_frameTable->rowCount())
        {
            return current_index.row();
        }

        if (const QItemSelectionModel* selection_model = m_frameTable->selectionModel())
        {
            const QModelIndexList selected_rows = selection_model->selectedRows(0);
            if (!selected_rows.isEmpty())
            {
                return selected_rows.first().row();
            }

            const QModelIndexList selected_indexes = selection_model->selectedIndexes();
            if (!selected_indexes.isEmpty())
            {
                return selected_indexes.first().row();
            }
        }

        return -1;
    }

    [[nodiscard]] int selectedDiagnosticRow() const
    {
        if (m_diagnosticsTable == nullptr)
        {
            return -1;
        }
        const int row = m_diagnosticsTable->currentRow();
        return row >= 0 && row < static_cast<int>(m_diagnostics.size()) ? row : -1;
    }

    [[nodiscard]] bool selectedDiagnosticCanBeAccepted() const
    {
        const int row = selectedDiagnosticRow();
        if (row < 0)
        {
            return false;
        }

        return m_diagnostics[static_cast<size_t>(row)].state.trimmed().compare(QStringLiteral("Candidate"),
                                                                               Qt::CaseInsensitive) == 0;
    }

    [[nodiscard]] bool hasPendingMarkForSelectedFrame() const
    {
        return m_pendingSourceFramePosition.has_value() && !selectedFrameId().isEmpty();
    }

    void updatePreviewForSelectedFrame()
    {
        if (m_previewPlaceholder == nullptr)
        {
            return;
        }

        std::optional<QSize> source_size;
        const QString frame_id = selectedFrameId();
        if (!frame_id.isEmpty() && m_framePreviewSizeProvider)
        {
            source_size = m_framePreviewSizeProvider(frame_id);
        }

        ++m_previewRequestId;
        m_previewStop.request_stop();
        m_previewStop = std::stop_source{};
        if (m_previewWatcher != nullptr)
        {
            m_previewWatcher->disconnect(this);
            m_previewWatcher->deleteLater();
            m_previewWatcher = nullptr;
        }

        if (frame_id.isEmpty())
        {
            m_previewPlaceholder->setSourceSize(std::nullopt);
            clearPendingSourceFramePosition();
            refreshStatus();
            return;
        }

        const QFileInfo frame_info(frame_id);
        if (!frame_info.exists() || !frame_info.isFile())
        {
            m_previewPlaceholder->setPreviewUnavailable(tr("Preview unavailable"), source_size);
            if (!source_size.has_value())
            {
                clearPendingSourceFramePosition();
            }
            refreshStatus();
            return;
        }

        const bool use_image_view_preview = m_previewPlaceholder->usesImageViewControl();
        if (use_image_view_preview)
        {
            m_previewPlaceholder->loadImageViewFile(frame_id, source_size);
            const quint64 request_id = m_previewRequestId;
            QTimer::singleShot(kPreviewLoadTimeoutMs, this, [this, request_id, frame_id, source_size]() mutable {
                if (request_id != m_previewRequestId || selectedFrameId() != frame_id ||
                    m_previewPlaceholder == nullptr || !m_previewPlaceholder->property("anPreviewLoading").toBool())
                {
                    return;
                }

                m_previewPlaceholder->setPreviewUnavailable(
                    tr("Preview load is taking longer than expected. Select this frame again to "
                       "retry."),
                    source_size);
                refreshStatus();
            });
            refreshStatus();
            return;
        }

        IAppContext* context = appContext();
        IEngineSessionProvider* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
        const an::consumer::EngineSession* session = provider != nullptr ? &provider->engineSession() : nullptr;
        if (session == nullptr)
        {
            m_previewPlaceholder->setPreviewUnavailable(tr("Preview unavailable"), source_size);
            refreshStatus();
            return;
        }

        m_previewPlaceholder->setLoading(source_size);
        const quint64 request_id = m_previewRequestId;
        const QSize max_size;
        // The setting is read here, on the GUI thread; the worker only reads the frame.
        const an::DemosaicMethod demosaic = cometTrackingPreviewDemosaicMethod();
        const std::stop_token stop = m_previewStop.get_token();
        auto* watcher = new QFutureWatcher<CometTrackingFramePreviewResult>(this);
        m_previewWatcher = watcher;
        connect(watcher, &QFutureWatcher<CometTrackingFramePreviewResult>::finished, this,
                [this, watcher, request_id, frame_id, source_size]() mutable {
                    const CometTrackingFramePreviewResult result = watcher->result();
                    if (m_previewWatcher == watcher)
                    {
                        m_previewWatcher = nullptr;
                    }
                    watcher->deleteLater();

                    if (request_id != m_previewRequestId || selectedFrameId() != frame_id ||
                        m_previewPlaceholder == nullptr)
                    {
                        return;
                    }

                    if (result.succeeded && !result.image.isNull())
                    {
                        m_previewPlaceholder->setPreviewImage(result.image, result.sourceSize);
                    }
                    else if (result.succeeded)
                    {
                        m_previewPlaceholder->setPreviewUnavailable(tr("Preview unavailable"), source_size);
                    }
                    else
                    {
                        m_previewPlaceholder->setPreviewUnavailable(result.message, source_size);
                        if (!source_size.has_value())
                        {
                            clearPendingSourceFramePosition();
                        }
                    }
                    refreshStatus();
                });
        QTimer::singleShot(kPreviewLoadTimeoutMs, this, [this, watcher, request_id, frame_id, source_size]() mutable {
            if (m_previewWatcher != watcher || request_id != m_previewRequestId || selectedFrameId() != frame_id ||
                m_previewPlaceholder == nullptr)
            {
                return;
            }

            m_previewWatcher = nullptr;
            m_previewStop.request_stop();
            watcher->disconnect(this);
            connect(watcher, &QFutureWatcher<CometTrackingFramePreviewResult>::finished, watcher,
                    &QObject::deleteLater);
            m_previewPlaceholder->setPreviewUnavailable(
                tr("Preview load is taking longer than expected. Select this frame again to retry."), source_size);
            refreshStatus();
        });
        watcher->setFuture(QtConcurrent::run([session, frame_id, demosaic, max_size, stop]() {
            return loadCometTrackingFramePreviewFromFile(*session, frame_id, demosaic, max_size, stop);
        }));
        refreshStatus();
    }

    void appendObservation(RegistrationSettings::CometTrackingObservation observation)
    {
        RegistrationSettings::CometTrackingSettings settings = cometTrackingSettings();
        settings.observations.push_back(std::move(observation));
        m_settings = settings;
        refreshObservationTable();
    }

    void markCurrentFrameObservation()
    {
        const QString frame_id = selectedFrameId();
        if (frame_id.isEmpty() || !m_pendingSourceFramePosition.has_value())
        {
            return;
        }

        RegistrationSettings::CometTrackingObservation observation;
        observation.frameId = frame_id;
        observation.sourcePosition = *m_pendingSourceFramePosition;
        observation.origin = RegistrationSettings::CometObservationOrigin::Manual;
        observation.enabled = true;
        appendObservation(std::move(observation));
        clearPendingSourceFramePosition();
    }

    void acceptSelectedDiagnosticCandidate()
    {
        const int row = selectedDiagnosticRow();
        if (row < 0 || !selectedDiagnosticCanBeAccepted())
        {
            return;
        }

        const CometTrackingDialogDiagnostic& diagnostic = m_diagnostics[static_cast<size_t>(row)];
        const QString candidate_id = diagnostic.candidateId.trimmed();
        if (!candidate_id.isEmpty())
        {
            RegistrationSettings::CometTrackingSettings settings = cometTrackingSettings();
            for (const CometTrackingDialogDiagnostic& candidate_diagnostic : m_diagnostics)
            {
                if (candidate_diagnostic.candidateId.trimmed() != candidate_id ||
                    candidate_diagnostic.state.trimmed().compare(QStringLiteral("Candidate"), Qt::CaseInsensitive) != 0)
                {
                    continue;
                }

                upsertAcceptedDiagnosticObservation(settings, candidate_diagnostic);
            }
            m_settings = settings;
            refreshObservationTable();
            return;
        }

        appendAcceptedDiagnosticObservation(diagnostic);
    }

    void attemptAcceptProductionFit()
    {
        if (canAcceptProductionFit())
        {
            accept();
            return;
        }

        m_autoTrackStatusMessage = tr("Add or accept a second comet observation before submitting.");
        refreshStatus();
    }

    void startAutoTracking()
    {
        if (!m_autoTrackHandler || m_autoTrackInProgress || m_blindDetectInProgress || acceptedObservationCount() <= 0)
        {
            return;
        }

        m_autoTrackInProgress = true;
        m_autoTrackStatusMessage = tr("Comet auto-tracking is running...");
        refreshStatus();

        QPointer<CometTrackingDialog> guard(this);
        m_autoTrackHandler(cometTrackingSettings(),
                           [guard](std::vector<CometTrackingDialogDiagnostic> diagnostics, QString message) {
                               if (!guard)
                               {
                                   return;
                               }
                               guard->finishAutoTracking(std::move(diagnostics), std::move(message));
                           });
    }

    void finishAutoTracking(std::vector<CometTrackingDialogDiagnostic> diagnostics, QString message)
    {
        m_autoTrackInProgress = false;
        m_autoTrackStatusMessage = std::move(message);
        setDiagnostics(std::move(diagnostics));
    }

    void startBlindDetection()
    {
        if (!m_blindDetectHandler || m_autoTrackInProgress || m_blindDetectInProgress)
        {
            return;
        }

        m_blindDetectInProgress = true;
        m_autoTrackStatusMessage = tr("Moving-object search is running...");
        refreshStatus();

        QPointer<CometTrackingDialog> guard(this);
        m_blindDetectHandler(cometTrackingSettings(),
                             [guard](std::vector<CometTrackingDialogDiagnostic> diagnostics, QString message) {
                                 if (!guard)
                                 {
                                     return;
                                 }
                                 guard->finishBlindDetection(std::move(diagnostics), std::move(message));
                             });
    }

    void finishBlindDetection(std::vector<CometTrackingDialogDiagnostic> diagnostics, QString message)
    {
        m_blindDetectInProgress = false;
        m_autoTrackStatusMessage = std::move(message);
        setDiagnostics(std::move(diagnostics));
    }

    static void upsertAcceptedDiagnosticObservation(RegistrationSettings::CometTrackingSettings& settings,
                                                    const CometTrackingDialogDiagnostic& diagnostic)
    {
        for (RegistrationSettings::CometTrackingObservation& observation : settings.observations)
        {
            if (sameFrameId(observation.frameId, diagnostic.frameId))
            {
                observation.frameId = diagnostic.frameId;
                observation.sourcePosition = diagnostic.sourcePosition;
                observation.origin = RegistrationSettings::CometObservationOrigin::AcceptedAutomatic;
                observation.enabled = true;
                return;
            }
        }

        RegistrationSettings::CometTrackingObservation observation;
        observation.frameId = diagnostic.frameId;
        observation.sourcePosition = diagnostic.sourcePosition;
        observation.origin = RegistrationSettings::CometObservationOrigin::AcceptedAutomatic;
        observation.enabled = true;
        settings.observations.push_back(std::move(observation));
    }

    void appendAcceptedDiagnosticObservation(const CometTrackingDialogDiagnostic& diagnostic)
    {
        RegistrationSettings::CometTrackingSettings settings = cometTrackingSettings();
        upsertAcceptedDiagnosticObservation(settings, diagnostic);
        m_settings = settings;
        refreshObservationTable();
    }

    void populateFrameTable()
    {
        m_frameTable->setRowCount(m_availableLightFrames.size());
        for (int row = 0; row < m_availableLightFrames.size(); ++row)
        {
            auto* item = new QTableWidgetItem(frameLabel(m_availableLightFrames.at(row)));
            item->setData(Qt::UserRole, m_availableLightFrames.at(row));
            m_frameTable->setItem(row, 0, item);
        }
    }

    void refreshObservationTable()
    {
        m_observationTable->blockSignals(true);
        m_observationTable->setRowCount(m_settings.observations.size());
        for (int row = 0; row < m_settings.observations.size(); ++row)
        {
            const auto& observation = m_settings.observations.at(row);

            auto* enabled_item = new QTableWidgetItem();
            enabled_item->setFlags((enabled_item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            enabled_item->setCheckState(observation.enabled ? Qt::Checked : Qt::Unchecked);
            m_observationTable->setItem(row, 0, enabled_item);

            auto* frame_item = new QTableWidgetItem(frameLabel(observation.frameId));
            frame_item->setData(Qt::UserRole, observation.frameId);
            m_observationTable->setItem(row, 1, frame_item);

            auto* x_item = new QTableWidgetItem(QString::number(observation.sourcePosition.x(), 'f', 3));
            x_item->setData(Qt::UserRole, observation.sourcePosition.x());
            m_observationTable->setItem(row, 2, x_item);

            auto* y_item = new QTableWidgetItem(QString::number(observation.sourcePosition.y(), 'f', 3));
            y_item->setData(Qt::UserRole, observation.sourcePosition.y());
            m_observationTable->setItem(row, 3, y_item);

            auto* origin_item = new QTableWidgetItem(originLabel(observation.origin));
            origin_item->setData(Qt::UserRole, static_cast<int>(observation.origin));
            m_observationTable->setItem(row, 4, origin_item);
        }
        m_observationTable->blockSignals(false);
        refreshStatus();
    }

    void refreshDiagnosticsTable()
    {
        m_diagnosticsTable->setRowCount(static_cast<int>(m_diagnostics.size()));
        for (int row = 0; row < static_cast<int>(m_diagnostics.size()); ++row)
        {
            const CometTrackingDialogDiagnostic& diagnostic = m_diagnostics[static_cast<size_t>(row)];
            m_diagnosticsTable->setItem(row, 0, new QTableWidgetItem(frameLabel(diagnostic.frameId)));
            m_diagnosticsTable->setItem(row, 1,
                                        new QTableWidgetItem(QString::number(diagnostic.sourcePosition.x(), 'f', 3)));
            m_diagnosticsTable->setItem(row, 2,
                                        new QTableWidgetItem(QString::number(diagnostic.sourcePosition.y(), 'f', 3)));
            m_diagnosticsTable->setItem(row, 3, new QTableWidgetItem(QString::number(diagnostic.confidence, 'f', 2)));
            const QString state_text = diagnostic.candidateId.trimmed().isEmpty()
                                           ? diagnostic.state
                                           : tr("%1 (%2)").arg(diagnostic.state, diagnostic.candidateId.trimmed());
            auto* state_item = new QTableWidgetItem(state_text);
            state_item->setToolTip(diagnostic.message);
            m_diagnosticsTable->setItem(row, 4, state_item);
        }
    }

    void refreshStatus()
    {
        if (m_statusLabel != nullptr)
        {
            const int enabled_count = acceptedObservationCount();
            const QString production_text =
                tr("%1 accepted observation(s). Production fit requires two accepted observations.").arg(enabled_count);
            QString next_step;
            if (selectedFrameId().isEmpty())
            {
                next_step = tr("Select a frame on the left to load its preview.");
            }
            else if (m_pendingSourceFramePosition.has_value())
            {
                next_step = tr("Pending comet core: X %1, Y %2.")
                                .arg(m_pendingSourceFramePosition->x(), 0, 'f', 1)
                                .arg(m_pendingSourceFramePosition->y(), 0, 'f', 1);
            }
            else if (enabled_count == 0)
            {
                next_step = tr("Click the comet core in a preview, then Mark. Start with a clear first or last frame.");
            }
            else if (enabled_count == 1)
            {
                next_step = tr("Add a second mark yourself, or use Auto Track and accept a candidate.");
            }
            else
            {
                next_step = tr("Use Auto Track to inspect candidate positions, then OK to save the accepted track.");
            }

            const QString status_text =
                m_autoTrackStatusMessage.trimmed().isEmpty()
                    ? QStringLiteral("%1 %2").arg(next_step, production_text)
                    : QStringLiteral("%1 %2 %3").arg(m_autoTrackStatusMessage, next_step, production_text);
            m_statusLabel->setText(status_text);
        }
        if (m_autoTrackButton != nullptr)
        {
            m_autoTrackButton->setEnabled(!m_autoTrackInProgress && !m_blindDetectInProgress && m_autoTrackHandler &&
                                          acceptedObservationCount() > 0);
        }
        if (m_findMovingObjectButton != nullptr)
        {
            m_findMovingObjectButton->setEnabled(!m_autoTrackInProgress && !m_blindDetectInProgress &&
                                                 m_blindDetectHandler);
        }
        if (m_markButton != nullptr)
        {
            m_markButton->setEnabled(hasPendingMarkForSelectedFrame());
        }
        if (m_clearPointButton != nullptr)
        {
            m_clearPointButton->setEnabled(m_pendingSourceFramePosition.has_value());
        }
        if (m_acceptButton != nullptr)
        {
            m_acceptButton->setEnabled(selectedDiagnosticCanBeAccepted());
        }
    }

    QStringList m_availableLightFrames;
    static constexpr int kPreviewLoadTimeoutMs = 30000;
    RegistrationSettings::CometTrackingSettings m_settings;
    std::vector<CometTrackingDialogDiagnostic> m_diagnostics;
    std::optional<QPointF> m_pendingSourceFramePosition;
    CometTrackingAutoTrackHandler m_autoTrackHandler;
    CometTrackingBlindDetectHandler m_blindDetectHandler;
    CometTrackingFramePreviewSizeProvider m_framePreviewSizeProvider;
    QFutureWatcher<CometTrackingFramePreviewResult>* m_previewWatcher = nullptr;
    quint64 m_previewRequestId = 0;

    /// Stops the preview read on its way; a new selection or the dialog's end stops it.
    std::stop_source m_previewStop;
    bool m_autoTrackInProgress = false;
    bool m_blindDetectInProgress = false;
    QString m_autoTrackStatusMessage;
    QLabel* m_statusLabel = nullptr;
    QTableWidget* m_frameTable = nullptr;
    CometTrackingPreviewPane* m_previewPlaceholder = nullptr;
    QTableWidget* m_observationTable = nullptr;
    QTableWidget* m_diagnosticsTable = nullptr;
    QPushButton* m_markButton = nullptr;
    QPushButton* m_clearPointButton = nullptr;
    QPushButton* m_refineButton = nullptr;
    QPushButton* m_autoTrackButton = nullptr;
    QPushButton* m_findMovingObjectButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_acceptButton = nullptr;
    QSpinBox* m_searchRadiusSpinBox = nullptr;
    QDoubleSpinBox* m_minimumSnrSpinBox = nullptr;
    QSpinBox* m_bootstrapFramesSpinBox = nullptr;
    QComboBox* m_trackModelComboBox = nullptr;
    QCheckBox* m_robustRejectionCheckBox = nullptr;
    QDoubleSpinBox* m_outlierSigmaSpinBox = nullptr;
};
} // namespace an::ui
