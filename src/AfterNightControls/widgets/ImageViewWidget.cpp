// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewWidget.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"
#include "session/CommitTicket.h"
#include "session/DerivedResultKey.h"
#include "session/DisplayProxy.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "session/ImageReads.h"
#include "widgets/ImageLensWidget.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/MaskRenderer.h"
#include "widgets/PresentationBridge.h"
#include "widgets/QuickZoomController.h"
#include "widgets/ZoomManager.h"
#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"
#include "controls/ImageViewThemeColors.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QThread>
#include <QThreadPool>
#include <QTransform>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <sstream>
#include <utility>
#include <vector>

#include "common.h"

namespace
{
constexpr int SAFE_WIDGET_SIZE_MAX = (std::min)(QWIDGETSIZE_MAX - 1, 1000000);

int sanitizeWidgetDimension(const qint64 value)
{
    if (value <= 0)
    {
        return 0;
    }

    return static_cast<int>((std::min<qint64>)(value, SAFE_WIDGET_SIZE_MAX));
}

QSize sanitizeWidgetSize(const QSize& size)
{
    return QSize(sanitizeWidgetDimension(size.width()), sanitizeWidgetDimension(size.height()));
}

QSize scaleWidgetSize(const QSize& baseSize, const double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
    {
        return QSize(0, 0);
    }

    const qint64 scaledWidth = static_cast<qint64>(std::llround(static_cast<double>(baseSize.width()) * factor));
    const qint64 scaledHeight = static_cast<qint64>(std::llround(static_cast<double>(baseSize.height()) * factor));

    return QSize(sanitizeWidgetDimension(scaledWidth), sanitizeWidgetDimension(scaledHeight));
}

/// The session the view's presentations run in, or null in a process that lends none.
[[nodiscard]] const an::consumer::EngineSession* viewSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

/// The time from a moment to now.
[[nodiscard]] std::chrono::nanoseconds elapsedSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
}

/// The canonical text of what an analysis depends on beyond the revision and the channel: the promotion and the colour
/// balance it was measured after.
[[nodiscard]] std::string analysisPolicyOf(const an::DisplayPresentationRequest& request)
{
    std::ostringstream text;
    text.precision(17);
    text << "promotion=" << static_cast<int>(request.promotion) << ";neutralize=" << request.neutralize << ';'
         << request.neutralizationBackground.low << ',' << request.neutralizationBackground.high
         << ";calibrate=" << request.calibrate << ';' << request.calibrationBackground.low << ','
         << request.calibrationBackground.high << ';' << request.calibrationWhite.low << ','
         << request.calibrationWhite.high;
    return text.str();
}

/// Whether a presentation measures anything of what it presents: a stretch's statistics or a colour balance.
[[nodiscard]] bool measuresWhatItPresents(const an::DisplayPresentationRequest& request) noexcept
{
    return request.stretch == an::PresentationStretch::Automatic || request.neutralize || request.calibrate;
}

/// The part of an image, in its own pixels, a presentation at a geometry covers: its region, or every whole block of
/// the factor.
[[nodiscard]] an::ui::ViewRegion coverOf(const an::ui::ViewDisplayGeometry& geometry, const QSize& image)
{
    if (geometry.region.has_value())
    {
        return *geometry.region;
    }
    const std::uint32_t factor = (std::max)(1u, geometry.proxyFactor);
    return an::ui::ViewRegion{.x = 0u,
                              .y = 0u,
                              .width = static_cast<std::uint32_t>(image.width()) / factor * factor,
                              .height = static_cast<std::uint32_t>(image.height()) / factor * factor};
}

/// Whether a covered part of an image holds what is shown, short of the strip at the right and bottom edges that no
/// whole block of the factor covers.
[[nodiscard]] bool holds(const an::ui::ViewRegion& cover, std::uint32_t factor, const an::ui::ViewRegion& shown,
                         const QSize& image)
{
    const std::uint64_t edgeX = static_cast<std::uint64_t>(image.width()) / factor * factor;
    const std::uint64_t edgeY = static_cast<std::uint64_t>(image.height()) / factor * factor;
    const std::uint64_t right = (std::min)(std::uint64_t{shown.x} + shown.width, edgeX);
    const std::uint64_t bottom = (std::min)(std::uint64_t{shown.y} + shown.height, edgeY);
    return shown.x >= cover.x && shown.y >= cover.y && right <= std::uint64_t{cover.x} + cover.width &&
           bottom <= std::uint64_t{cover.y} + cover.height;
}

} // namespace

/// One presentation on its way: what it presents, the ticket that decides whether it may be painted, the keys it is
/// filed under, and the mask shown over it, which is presented with it at its reduction and over its region.
struct ImageViewWidget::PresentationJob
{
    std::shared_ptr<const an::consumer::ImageAsset> asset;
    an::ui::CommitTicket ticket;
    an::ui::ViewPresentationSettings settings;
    std::optional<an::DisplayPresentationAnalysis> reuse;
    an::ui::DerivedResultKey key;
    an::ui::DerivedResultKey analysisKey;
    std::shared_ptr<const an::ui::DisplayProxy> proxy;
    std::shared_ptr<const an::consumer::ImageAsset> mask;
    an::ui::ViewMaskOverlaySettings maskSettings;
    std::shared_ptr<const an::ui::DisplayProxy> maskProxy;
};

/// What a presentation came to on its worker: the bytes and the mask's layer, the proxies they were presented from,
/// and what it cost.
struct ImageViewWidget::PresentationResult
{
    an::Result<an::ui::ViewPresentationOutcome> outcome{
        an::Status::failure(an::ErrorCode::Internal, "the presentation did not run")};
    std::optional<std::chrono::nanoseconds> presentTime;
    std::shared_ptr<const an::ui::DisplayProxy> proxy;
    std::shared_ptr<const an::ui::DisplayProxy> maskProxy;
    std::vector<std::chrono::nanoseconds> reductions;
    an::Result<std::shared_ptr<const an::ui::PresentedImage>> maskLayer{
        an::Status::failure(an::ErrorCode::Internal, "the mask layer was not rendered")};
    QImage maskImage;
};

struct ImageViewWidget::Impl
{
    ImageViewMode m_ViewMode = ImageViewMode::LINEAR_MODE;
    an::ui::ViewChannel m_Channel{};

    // What is painted, the part of the image it covers and the reduction it was presented at, and what it was presented
    // from and through. The bytes are painted as they are, wrapped, never converted into a pixmap.
    std::shared_ptr<const an::ui::PresentedImage> m_Presented;
    QImage m_PresentedImage;
    an::ui::DerivedResultKey m_PresentedKey;
    std::uint32_t m_PresentedFactor = 1u;
    an::ui::ViewRegion m_PresentedCover{};
    std::optional<an::DisplayPresentationAnalysis> m_PresentedAnalysis;
    QSize m_ImageSize;

    // The mask layer, presented with the image over the same part at the same reduction, premultiplied on its worker.
    std::shared_ptr<const an::ui::PresentedImage> m_MaskLayer;
    QImage m_MaskImage;

    // A real-time preview frame's part of the document, painted over the presentation until the next one lands: its
    // bytes, the part it covers and the reduction it was presented at.
    std::shared_ptr<const an::ui::PresentedImage> m_Layer;
    QImage m_LayerImage;
    an::ui::ViewRegion m_LayerCover{};
    std::uint32_t m_LayerFactor = 1u;

    // Whether what is painted was adopted from a real-time preview's frame rather than presented by the view itself:
    // such bytes are filed under the key the view's own request would make, so only `presentDocumentAgain` replaces
    // them before the document or the settings change.
    bool m_PaintsAdopted = false;

    // The display proxies presentations were made from, kept by revision and factor so a zoom, a pan or a channel
    // change reduces nothing again.
    an::ui::DisplayProxyCache m_Proxies{2u};
    an::ui::DisplayProxyCache m_MaskProxies{2u};

    // The analysis kept for the revision it was measured on, and one a caller supplied for the next presentation.
    struct KeptAnalysis
    {
        an::ui::DerivedResultKey key;
        an::DisplayPresentationAnalysis analysis;
    };
    std::optional<KeptAnalysis> m_Analysis;
    std::optional<an::DisplayPresentationAnalysis> m_Precomputed;

    an::ui::RequestTracker m_Presentations;
    std::optional<an::ui::DerivedResultKey> m_InFlightKey;
    std::uint64_t m_InFlightRequest = 0u;
    an::ui::ViewDisplayGeometry m_InFlightGeometry{};
    bool m_WatchingViewport = false;
    // Set while a request is decided, which may resize the widget and so move the viewport under it.
    bool m_Requesting = false;
    std::shared_ptr<std::atomic<std::uint32_t>> m_Outstanding = std::make_shared<std::atomic<std::uint32_t>>(0u);
    ImageViewPresentationTelemetry m_Telemetry;

    double m_CurrentScale = 1.0;

    bool m_IsInRotationPreview = false;
    double m_RotationPreviewAngle = 0.0;
    QRect m_OriginalWidgetRectForRotations;
    bool m_RotationResizeArea = false;

    bool m_IsSelectionAreaEnabled = false;
    QRect m_SelectionArea;
    QRect m_ScaledSelectionArea;
    QString m_SelectionAreaText;

    ImageViewSelectionStyle m_ImageViewSelectionStyle = ImageViewSelectionStyle::CROP_STYLE;

    bool m_QuickZoomEnabled = true;
    bool m_MouseLeftButtonPressed = false;
    std::unique_ptr<ImageLensWidget> m_ImageLensWidget;
    QTimer* m_QuickZoomTimer = nullptr;
    int m_QuickZoomDelayMs = 300;

    bool m_AutostretchStatisticsCachingEnabled = true;

    bool m_IsAlternateImageRenderEnabled = false;
    bool m_IsAlternateImageForQuickZoomRenderEnabled = false;
    ImageViewWidget::AlternateImageRenderingChangedCallback m_AlternateImageRenderingChangedCallback;

    std::shared_ptr<const an::ui::PresentedImage> m_QuickZoomImageOverride;

    std::deque<ImageViewWidgetDisplayModule*> m_DisplayModules;

    bool m_PerformanceMode = false;

    AutostretchParams m_AutostretchParams;
};

static_assert(sizeof(ImageViewWidget) <= sizeof(QWidget) + sizeof(void*) * 3,
              "ImageViewWidget should stay thin: its state lives behind the pimpl.");

ImageViewWidget::ImageViewWidget(ImageViewControl* control)
    : QWidget(nullptr), ImageViewControlListener(control), m_impl(std::make_unique<Impl>())
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    applyThemePalette();
    m_impl->m_ImageLensWidget = std::make_unique<ImageLensWidget>(nullptr);
    m_impl->m_ImageLensWidget->hide();

    m_impl->m_QuickZoomTimer = new QTimer(this);

    connect(m_impl->m_QuickZoomTimer, &QTimer::timeout, this, &ImageViewWidget::showImageLensWidget);
    m_impl->m_QuickZoomTimer->setSingleShot(true);
}

void ImageViewWidget::applyThemePalette()
{
    QPalette viewPalette = palette();
    const QColor backgroundColor = an::ui::imageViewScrollAreaWindowColor(an::ui::isDarkTheme());
    viewPalette.setColor(QPalette::Window, backgroundColor);
    viewPalette.setColor(QPalette::Base, backgroundColor);
    viewPalette.setColor(QPalette::Dark, backgroundColor);
    setPalette(viewPalette);
}

bool ImageViewWidget::event(QEvent* ev)
{
    if (ev->type() == QEvent::DevicePixelRatioChange && m_impl && m_image_view_control != nullptr)
    {
        // The presentation is made at the size the view paints in device pixels: a window moved to a screen of another
        // ratio paints a presentation of another size, and asks for it at once rather than at the next zoom or pan.
        requestPresentation();
    }
    return QWidget::event(ev);
}

void ImageViewWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
    {
        applyThemePalette();
        if (m_impl && m_impl->m_ImageLensWidget)
        {
            m_impl->m_ImageLensWidget->update();
        }
        update();
    }

    QWidget::changeEvent(ev);
}

void ImageViewWidget::leaveEvent(QEvent* ev)
{
    if (m_image_view_control)
    {
        m_image_view_control->clearHoveredCelestialCoords();
    }

    QWidget::leaveEvent(ev);
}

void ImageViewWidget::displayMainImage()
{
    displayChannel(an::ui::ViewChannel{});
}

void ImageViewWidget::displayChannel(an::ui::ViewChannel channel)
{
    // The channel shown already asks for nothing: an update of the image asks for its own presentation through the
    // view's listener, and a second request would only be coalesced into it.
    if (channel == m_impl->m_Channel && (m_impl->m_Presented || m_impl->m_InFlightKey.has_value()))
    {
        return;
    }
    m_impl->m_Channel = channel;
    refreshDisplayImage();
}

an::ui::ViewChannel ImageViewWidget::displayedChannel() const
{
    return m_impl->m_Channel;
}

void ImageViewWidget::refreshDisplayImage()
{
    requestPresentation();
}

void ImageViewWidget::fitInView()
{
    if (m_impl->m_ImageSize.isEmpty())
        return;

    auto scrrollArea = m_image_view_control->getScrollArea();
    auto vpRect = scrrollArea->viewport()->rect();

    auto hsBar = scrrollArea->horizontalScrollBar();
    auto vsBar = scrrollArea->verticalScrollBar();

    if (hsBar->isVisible())
    {
        auto hsBarRect = hsBar->rect();
        vpRect.setHeight(vpRect.height() + hsBarRect.height());
    }

    if (vsBar->isVisible())
    {
        auto vsBarRect = vsBar->rect();
        vpRect.setWidth(vpRect.width() + vsBarRect.width());
    }

    const QSize imgSize = m_impl->m_ImageSize;

    if (vpRect.width() <= 0 || vpRect.height() <= 0 || imgSize.width() <= 0 || imgSize.height() <= 0)
    {
        return;
    }

    double factorX = (double)vpRect.width() / (double)imgSize.width();
    double factorY = (double)vpRect.height() / (double)imgSize.height();

    double factor = factorX;

    if (factor > factorY)
    {
        factor = factorY;
    }

    scaleImageView(factor, true, true);
}

void ImageViewWidget::zoomIn()
{
    const double factor = 1.25;
    auto scrrollArea = m_image_view_control->getScrollArea();
    auto hBarPos = scrrollArea->horizontalScrollBar()->value();
    auto vBarPos = scrrollArea->verticalScrollBar()->value();

    auto vpRect = scrrollArea->viewport()->rect();
    auto pos = vpRect.size() / 2.f;
    int offsetx = hBarPos + (int)pos.width();
    int offsety = vBarPos + (int)pos.height();
    const double appliedFactor = an::ui::ZoomManager::effectiveRelativeZoomFactor(m_impl->m_CurrentScale, factor);

    scaleImageView(factor);

    if (std::abs(appliedFactor - 1.0) > 1.0e-12)
    {
        scrrollArea->horizontalScrollBar()->setValue(
            static_cast<int>(std::lround(offsetx * appliedFactor - (scrrollArea->viewport()->rect().width() / 2.0))));
        scrrollArea->verticalScrollBar()->setValue(
            static_cast<int>(std::lround(offsety * appliedFactor - (scrrollArea->viewport()->rect().height() / 2.0))));
    }
}

void ImageViewWidget::zoomOut()
{
    const double factor = 0.8;
    auto scrrollArea = m_image_view_control->getScrollArea();
    auto hBarPos = scrrollArea->horizontalScrollBar()->value();
    auto vBarPos = scrrollArea->verticalScrollBar()->value();

    auto vpRect = scrrollArea->viewport()->rect();
    auto pos = vpRect.size() / 2.0;
    int offsetx = hBarPos + (int)pos.width();
    int offsety = vBarPos + (int)pos.height();
    const double appliedFactor = an::ui::ZoomManager::effectiveRelativeZoomFactor(m_impl->m_CurrentScale, factor);

    scaleImageView(factor);

    if (std::abs(appliedFactor - 1.0) > 1.0e-12)
    {
        scrrollArea->horizontalScrollBar()->setValue(
            static_cast<int>(std::lround(offsetx * appliedFactor - (scrrollArea->viewport()->rect().width() / 2.0))));
        scrrollArea->verticalScrollBar()->setValue(
            static_cast<int>(std::lround(offsety * appliedFactor - (scrrollArea->viewport()->rect().height() / 2.0))));
    }
}

void ImageViewWidget::zoom100()
{
    scaleImageView(1.0, true);
}

void ImageViewWidget::scaleImageView(double factor, const bool absolute, const bool forceUpdate)
{
    const double previousScale = m_impl->m_CurrentScale;

    if (absolute)
    {
        m_impl->m_CurrentScale = an::ui::ZoomManager::clampScale(factor);
    }
    else
    {
        m_impl->m_CurrentScale = an::ui::ZoomManager::applyRelativeZoom(m_impl->m_CurrentScale, factor);
    }

    const bool scaleChanged = std::abs(previousScale - m_impl->m_CurrentScale) >= 1.0e-12;
    if (!scaleChanged && !forceUpdate)
    {
        return;
    }

    updateImageScale();

    // A presentation is made at the size the view paints; another zoom may ask for another reduction or region.
    if (scaleChanged)
    {
        requestPresentation();
    }

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    if (scaleChanged && m_image_view_control)
    {
        m_image_view_control->notifyImageViewScaled();
    }
}

void ImageViewWidget::setViewMode(ImageViewMode mode)
{
    if (m_impl->m_ViewMode != mode)
    {
        m_impl->m_ViewMode = mode;

        imageUpdated(false, true);

        for (auto& module : m_impl->m_DisplayModules)
        {
            module->onDisplayModeChanged(mode);
        }
    }
}

double ImageViewWidget::getCurrentZoom()
{
    return 100.0 * m_impl->m_CurrentScale;
}

double ImageViewWidget::getCurrentScaleFactor()
{
    return m_impl->m_CurrentScale;
}

ImageViewMode ImageViewWidget::getViewMode() const
{
    return m_impl->m_ViewMode;
}

ImageChannelViewMode ImageViewWidget::getChannelViewMode() const
{
    return an::ui::channelViewModeFor(m_impl->m_Channel);
}

void ImageViewWidget::onPreImageLoaded(ImageViewControl* ctrl) {}

void ImageViewWidget::onImageLoaded(ImageViewControl* ctrl)
{
    imageUpdated(true, false);
}

void ImageViewWidget::onPreImageUpdated(ImageViewControl* ctrl) {}

void ImageViewWidget::onImageUpdated(ImageViewControl* ctrl)
{
    imageUpdated(true, false);
}

void ImageViewWidget::onMaskUpdated(ImageViewControl* ctrl)
{
    requestMaskOverlay();
}

void ImageViewWidget::enableSelectionArea(bool enable)
{
    m_impl->m_IsSelectionAreaEnabled = enable;
}

bool ImageViewWidget::isSelectionAreaEnabled() const
{
    return m_impl->m_IsSelectionAreaEnabled;
}

void ImageViewWidget::updateSelectionArea(const QRect& rect)
{
    m_impl->m_SelectionArea = rect;
    checkSelectionAreaBoundaries();

    m_impl->m_ScaledSelectionArea =
        checkRectSize(scaleRect(m_impl->m_SelectionArea, m_impl->m_CurrentScale), this->rect());
}

void ImageViewWidget::resetSelectionArea()
{
    if (m_impl->m_IsInRotationPreview && m_impl->m_RotationPreviewAngle != 0.0)
    {
        m_impl->m_SelectionArea = QRect(0, 0, 0, 0);
        m_impl->m_SelectionArea.setSize(getCurrentRotatedAreaSize());
    }
    else
    {
        m_impl->m_SelectionArea = QRect(QPoint(0, 0), m_impl->m_ImageSize);
    }

    m_impl->m_ScaledSelectionArea =
        checkRectSize(scaleRect(m_impl->m_SelectionArea, m_impl->m_CurrentScale), this->rect());
}

QRect ImageViewWidget::getCurrentSelectionArea() const
{
    return m_impl->m_SelectionArea;
}

QRect ImageViewWidget::getCurrentScaledSelectionArea() const
{
    return m_impl->m_ScaledSelectionArea;
}

bool ImageViewWidget::changeSelectionAreaX(int xpos)
{
    if (xpos < 0 || xpos >= m_impl->m_ScaledSelectionArea.x() + m_impl->m_ScaledSelectionArea.width())
        return false;

    if (m_impl->m_ScaledSelectionArea.x() == xpos)
        return false;

    m_impl->m_ScaledSelectionArea.setX(xpos);
    m_impl->m_SelectionArea.setX(xpos * (1.0 / m_impl->m_CurrentScale));
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);

    return true;
}

bool ImageViewWidget::changeSelectionAreaWidth(int width)
{
    if (m_impl->m_ScaledSelectionArea.x() + width > this->width())
    {
        if (m_impl->m_IsInRotationPreview && m_impl->m_RotationPreviewAngle != 0.0)
        {
            m_impl->m_SelectionArea.setWidth(getCurrentRotatedAreaSize().width() - m_impl->m_SelectionArea.x());
        }
        else
        {
            m_impl->m_SelectionArea.setWidth(m_impl->m_ImageSize.width() - m_impl->m_SelectionArea.x());
        }

        return false;
    }
    else if (width < 1)
    {
        m_impl->m_SelectionArea.setWidth(1);
        return false;
    }

    m_impl->m_ScaledSelectionArea.setWidth(width);
    m_impl->m_SelectionArea.setWidth(width * (1.0 / m_impl->m_CurrentScale));
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    return true;
}

bool ImageViewWidget::changeSelectionAreaY(int ypos)
{
    if (ypos < 0 || ypos >= m_impl->m_ScaledSelectionArea.y() + m_impl->m_ScaledSelectionArea.height())
        return false;

    if (m_impl->m_ScaledSelectionArea.y() == ypos)
        return false;

    m_impl->m_ScaledSelectionArea.setY(ypos);
    m_impl->m_SelectionArea.setY(ypos * (1.0 / m_impl->m_CurrentScale));
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    return true;
}

bool ImageViewWidget::changeSelectionAreaHeight(int height)
{
    if (m_impl->m_ScaledSelectionArea.y() + height > this->height())
    {
        if (m_impl->m_IsInRotationPreview && m_impl->m_RotationPreviewAngle != 0.0)
        {
            m_impl->m_SelectionArea.setHeight(getCurrentRotatedAreaSize().height() - m_impl->m_SelectionArea.y());
        }
        else
        {
            m_impl->m_SelectionArea.setHeight(m_impl->m_ImageSize.height() - m_impl->m_SelectionArea.y());
        }

        return false;
    }
    else if (height < 1)
    {
        m_impl->m_SelectionArea.setHeight(1);
        return false;
    }

    m_impl->m_ScaledSelectionArea.setHeight(height);
    m_impl->m_SelectionArea.setHeight(height * (1.0 / m_impl->m_CurrentScale));
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    return true;
}

bool ImageViewWidget::moveSelectionArea(int xpos, int ypos)
{
    if ((xpos < 0) || (xpos + m_impl->m_ScaledSelectionArea.width() > this->width()))
        return false;

    if ((ypos < 0) || (ypos + m_impl->m_ScaledSelectionArea.height() > this->height()))
        return false;

    m_impl->m_ScaledSelectionArea.moveLeft(xpos);
    m_impl->m_SelectionArea.moveLeft(xpos * (1.0 / m_impl->m_CurrentScale));

    m_impl->m_ScaledSelectionArea.moveTop(ypos);
    m_impl->m_SelectionArea.moveTop(ypos * (1.0 / m_impl->m_CurrentScale));

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    return true;
}

void ImageViewWidget::setImageViewSelectionStyle(ImageViewSelectionStyle style)
{
    m_impl->m_ImageViewSelectionStyle = style;
}

void ImageViewWidget::setSelectionAreaText(const QString& text)
{
    m_impl->m_SelectionAreaText = text;
}

QString ImageViewWidget::getSelectionAreaText() const
{
    return m_impl->m_SelectionAreaText;
}

void ImageViewWidget::clearSelectionAreaText()
{
    m_impl->m_SelectionAreaText.clear();
}

ImageViewSelectionStyle ImageViewWidget::getImageViewSelectionStyle() const
{
    return m_impl->m_ImageViewSelectionStyle;
}

void ImageViewWidget::enableRotationPreview(const bool enable)
{
    m_impl->m_IsInRotationPreview = enable;
    if (m_impl->m_IsInRotationPreview)
    {
        m_impl->m_OriginalWidgetRectForRotations = this->rect();
    }
    else
    {
        const QSize safeSize = sanitizeWidgetSize(m_impl->m_OriginalWidgetRectForRotations.size());
        SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, setFixedSize, safeSize);
    }

    updateImageScale();

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool ImageViewWidget::isRotationPreviewModeEnabled() const
{
    return m_impl->m_IsInRotationPreview;
}

void ImageViewWidget::setRotationPreviewAngle(const double angle)
{
    if (m_impl->m_RotationPreviewAngle == angle)
        return;

    m_impl->m_RotationPreviewAngle = angle;

    if (m_impl->m_IsInRotationPreview && m_impl->m_RotationResizeArea)
    {
        QRect currentRect = m_impl->m_OriginalWidgetRectForRotations;
        QPointF center = currentRect.center();

        QTransform transform;
        transform.translate(center.x(), center.y());
        transform.rotate(m_impl->m_RotationPreviewAngle);
        transform.translate(-center.x(), -center.y());

        // Transform each corner of the QRect
        QPointF topLeft = transform.map(currentRect.topLeft());
        QPointF topRight = transform.map(currentRect.topRight());
        QPointF bottomLeft = transform.map(currentRect.bottomLeft());
        QPointF bottomRight = transform.map(currentRect.bottomRight());

        // Calculate the minimum and maximum coordinates
        qreal minX = (std::min)({topLeft.x(), topRight.x(), bottomLeft.x(), bottomRight.x()});
        qreal minY = (std::min)({topLeft.y(), topRight.y(), bottomLeft.y(), bottomRight.y()});
        qreal maxX = (std::max)({topLeft.x(), topRight.x(), bottomLeft.x(), bottomRight.x()});
        qreal maxY = (std::max)({topLeft.y(), topRight.y(), bottomLeft.y(), bottomRight.y()});

        // Calculate the width and height of the bounding box
        const qreal width = maxX - minX;
        const qreal height = maxY - minY;

        const QSize safeSize =
            sanitizeWidgetSize(QSize(static_cast<int>(std::llround(width)), static_cast<int>(std::llround(height))));
        SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, setFixedSize, safeSize);
    }

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void ImageViewWidget::setRotationPreviewResizeArea(const bool enable)
{
    if (m_impl->m_RotationResizeArea != enable)
    {
        m_impl->m_RotationResizeArea = enable;

        if (enable)
        {
            double angle = m_impl->m_RotationPreviewAngle;
            m_impl->m_RotationPreviewAngle = 0;
            m_impl->m_OriginalWidgetRectForRotations = this->rect();
            setRotationPreviewAngle(angle);
        }
        else
        {
            const QSize safeSize = sanitizeWidgetSize(m_impl->m_OriginalWidgetRectForRotations.size());
            SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, setFixedSize, safeSize);
        }

        SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    }
}

QSize ImageViewWidget::getCurrentRotatedAreaSize()
{
    return this->rect().size() * (1.0 / m_impl->m_CurrentScale);
}

QSize ImageViewWidget::getCurrentRotatedScaledAreaSize()
{
    return this->rect().size();
}

void ImageViewWidget::enableQuickZoom(const bool enable)
{
    m_impl->m_QuickZoomEnabled = enable;
}

bool ImageViewWidget::isQuickZoomEnabled() const
{
    return m_impl->m_QuickZoomEnabled;
}

void ImageViewWidget::enableAlternateImageRendering(const bool enable)
{
    const bool changed = enable != m_impl->m_IsAlternateImageRenderEnabled;
    if (changed)
    {
        m_impl->m_IsAlternateImageRenderEnabled = enable;
        imageUpdated(true, true);
    }

    if (enable)
    {
        m_image_view_control->notifyAlternateImageEnabled();
    }
    else
    {
        m_image_view_control->notifyAlternateImageDisabled();
    }

    m_image_view_control->setRTPreviewShowOriginalLabel(enable);

    if (changed && m_impl->m_AlternateImageRenderingChangedCallback)
    {
        m_impl->m_AlternateImageRenderingChangedCallback(enable);
    }
}

bool ImageViewWidget::isAlternateImageRenderEnabled() const
{
    return m_impl->m_IsAlternateImageRenderEnabled;
}

void ImageViewWidget::setAlternateImageRenderingChangedCallback(AlternateImageRenderingChangedCallback callback)
{
    m_impl->m_AlternateImageRenderingChangedCallback = std::move(callback);
}

void ImageViewWidget::addDisplayModule(ImageViewWidgetDisplayModule* module)
{
    m_impl->m_DisplayModules.push_back(module);
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void ImageViewWidget::removeDisplayModule(ImageViewWidgetDisplayModule* module)
{
    auto it = std::find(m_impl->m_DisplayModules.begin(), m_impl->m_DisplayModules.end(), module);
    if (it != m_impl->m_DisplayModules.end())
    {
        m_impl->m_DisplayModules.erase(it);
        SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
    }
}

size_t ImageViewWidget::getNumOfDisplayModules() const
{
    return m_impl->m_DisplayModules.size();
}

bool ImageViewWidget::isContextMenuSupported() const
{
    if (m_impl->m_MouseLeftButtonPressed)
        return false;

    for (auto& module : m_impl->m_DisplayModules)
    {
        if (!module->supportsContextMenu())
            return false;
    }

    return true;
}

void ImageViewWidget::enablePerformanceMode(bool enabled)
{
    m_impl->m_PerformanceMode = enabled;
}

bool ImageViewWidget::isPerformanceModeEnabled() const
{
    return m_impl->m_PerformanceMode;
}

void ImageViewWidget::enableAutostretchStatisticsCaching(const bool enabled)
{
    if (m_impl->m_AutostretchStatisticsCachingEnabled == enabled)
    {
        return;
    }

    m_impl->m_AutostretchStatisticsCachingEnabled = enabled;

    // Force a clean re-computation next time autostretch is applied.
    m_impl->m_Analysis.reset();
    if (!enabled)
    {
        m_impl->m_Precomputed.reset();
    }
}

bool ImageViewWidget::isAutostretchStatisticsCachingEnabled() const
{
    return m_impl->m_AutostretchStatisticsCachingEnabled;
}

std::shared_ptr<const an::ui::PresentedImage> ImageViewWidget::presentedImage() const
{
    return m_impl->m_Presented;
}

QRect ImageViewWidget::presentedRegion() const
{
    if (!m_impl->m_Presented)
    {
        return QRect();
    }
    const an::ui::ViewRegion& cover = m_impl->m_PresentedCover;
    return QRect(static_cast<int>(cover.x), static_cast<int>(cover.y), static_cast<int>(cover.width),
                 static_cast<int>(cover.height));
}

std::shared_ptr<const an::ui::PresentedImage> ImageViewWidget::maskLayer() const
{
    return m_impl->m_MaskLayer;
}

std::optional<an::DisplayPresentationAnalysis> ImageViewWidget::presentedAnalysis() const
{
    return m_impl->m_PresentedAnalysis;
}

std::optional<an::DisplayPresentationAnalysis>
ImageViewWidget::keptAnalysisOf(const an::consumer::AssetRevision& revision,
                                const an::ui::ViewPresentationSettings& settings) const
{
    if (!m_impl->m_Analysis.has_value())
    {
        return std::nullopt;
    }
    // Whichever document held the asset, an analysis of the same revision under the same request and channel is the
    // same measurement.
    const an::ui::DerivedResultKey& kept = m_impl->m_Analysis->key;
    const an::ui::DerivedResultKey asked =
        an::ui::derivedResultKey(an::ui::DocumentRevision{.documentIdentity = kept.documentIdentity, .asset = revision},
                                 {}, an::ui::viewChannelKey(settings.channel), analysisPolicyOf(settings.request));
    if (kept != asked)
    {
        return std::nullopt;
    }
    return m_impl->m_Analysis->analysis;
}

QSize ImageViewWidget::imageSize() const
{
    return m_impl->m_ImageSize;
}

QSize ImageViewWidget::getRenderImageSize() const
{
    return scaleWidgetSize(sanitizeWidgetSize(m_impl->m_ImageSize), m_impl->m_CurrentScale);
}

QPointF ImageViewWidget::mapWidgetPointToImagePixel(const QPoint& pos) const
{
    if (m_impl->m_ImageSize.isEmpty())
    {
        return QPointF(-1.0, -1.0);
    }

    const double scale = m_impl->m_CurrentScale;
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return QPointF(-1.0, -1.0);
    }

    const double pixel_x = static_cast<double>(pos.x()) / scale;
    const double pixel_y = static_cast<double>(pos.y()) / scale;
    if (pixel_x < 0.0 || pixel_y < 0.0 || pixel_x >= static_cast<double>(m_impl->m_ImageSize.width()) ||
        pixel_y >= static_cast<double>(m_impl->m_ImageSize.height()))
    {
        return QPointF(-1.0, -1.0);
    }

    return QPointF(pixel_x, pixel_y);
}

void ImageViewWidget::setQuickZoomDelayMs(const int ms)
{
    m_impl->m_QuickZoomDelayMs = ms;
}

int ImageViewWidget::getQuickZoomDelayMs() const
{
    return m_impl->m_QuickZoomDelayMs;
}

void ImageViewWidget::setQuickZoomImageOverride(std::shared_ptr<const an::ui::PresentedImage> image)
{
    m_impl->m_QuickZoomImageOverride = std::move(image);
}

void ImageViewWidget::clearQuickZoomImageOverride()
{
    m_impl->m_QuickZoomImageOverride.reset();
}

void ImageViewWidget::cancelQuickZoom()
{
    m_impl->m_MouseLeftButtonPressed = false;
    m_impl->m_QuickZoomTimer->stop();
    m_impl->m_ImageLensWidget->hide();
    m_impl->m_ImageLensWidget->detachFromOwner();
}

void ImageViewWidget::setAutostretchSettings(const AutostretchParams& params, bool async)
{
    const bool balanceChanged = m_impl->m_AutostretchParams.neutralizeBackground != params.neutralizeBackground ||
                                m_impl->m_AutostretchParams.colorCalibration != params.colorCalibration ||
                                m_impl->m_AutostretchParams.linkedChannels != params.linkedChannels;

    m_impl->m_AutostretchParams = params;

    for (auto* module : m_impl->m_DisplayModules)
    {
        if (module)
        {
            module->onAutostretchContextChanged();
        }
    }

    if (async)
    {
        imageUpdatedAsync(balanceChanged, true);
    }
    else
    {
        imageUpdated(balanceChanged, true);
    }
}

AutostretchParams ImageViewWidget::getAutostretchSettings() const
{
    return m_impl->m_AutostretchParams;
}

an::DisplayPresentationRequest ImageViewWidget::presentationRequest() const
{
    const AutostretchParams& params = m_impl->m_AutostretchParams;
    an::DisplayPresentationRequest request;
    const bool autostretch = m_impl->m_ViewMode == ImageViewMode::AUTOSTRETCH_MODE;
    request.stretch = autostretch ? an::PresentationStretch::Automatic : an::PresentationStretch::Linear;
    // Every sample is carried at its full precision through the stretch: holding it to sixteen-bit levels changes no
    // byte of an unsigned eight- or sixteen-bit image, saves nothing, and bands a floating one.
    request.promotion = an::PresentationPromotion::Gradient;
    if (autostretch)
    {
        request.linked = params.linkedChannels;
        // A balance only means something when the stretch that follows is linked; an unlinked stretch undoes it.
        request.neutralize = params.neutralizeBackground && params.linkedChannels;
        request.calibrate = params.colorCalibration && params.linkedChannels;
        request.shadowsClipping = params.shadowsClipping;
        request.targetBackground = params.targetBackground;
        request.saturation = params.saturation;
        request.saturationBackgroundFactor = params.saturationBackground;
        request.saturationBackgroundLocation = m_image_view_control != nullptr && m_image_view_control->isSequence()
                                                   ? an::SaturationBackgroundLocation::Mean
                                                   : an::SaturationBackgroundLocation::Median;
        request.brightness = an::ui::presentationBrightnessOffset(params.brightness);
        request.contrast = an::ui::presentationContrastFactor(params.contrast);
    }
    // The bytes Qt paints without converting them; a multiply reads what the view shows as grey or RGB bytes instead.
    const bool multiplyMask = m_image_view_control != nullptr && m_image_view_control->isMasked() &&
                              m_image_view_control->isShowMaskEnabled() &&
                              m_image_view_control->getMaskRenderingMode() == MaskRenderingMode::multiply_mode;
    request.output = multiplyMask ? an::PresentationOutput::Interleaved8 : an::PresentationOutput::Bgra8;
    return request;
}

QRect ImageViewWidget::getCurrentImageArea()
{
    return QRect(QPoint(0, 0), m_impl->m_ImageSize);
}

QRect ImageViewWidget::getCurrentScaledImageArea()
{
    return QRect(0, 0, this->rect().width(), this->rect().height());
}

void ImageViewWidget::imageUpdated(bool recomputeStatistics, bool refreshDisplay)
{
    (void)refreshDisplay;
    if (QThread::currentThread() != thread())
    {
        imageUpdatedAsync(recomputeStatistics, refreshDisplay);
        return;
    }
    // A kept analysis is keyed by the revision, the channel, the promotion and the balance it was measured on and
    // under, so one that no longer fits is never reused and one that still fits is kept: setting it aside here would
    // measure the same image again.
    requestPresentation();
}

void ImageViewWidget::imageUpdatedAsync(bool recomputeStatistics, bool refreshDisplay)
{
    // Queued to the widget's own thread and never waited on: a caller on the GUI thread would otherwise wait for an
    // event only its own return lets run.
    QPointer<ImageViewWidget> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard, recomputeStatistics, refreshDisplay]() {
            if (guard)
            {
                guard->imageUpdated(recomputeStatistics, refreshDisplay);
            }
        },
        Qt::QueuedConnection);
}

void ImageViewWidget::setPrecomputedAnalysis(std::optional<an::DisplayPresentationAnalysis> analysis,
                                             const bool autostretchReady)
{
    (void)autostretchReady;
    if (!m_impl->m_AutostretchStatisticsCachingEnabled)
    {
        m_impl->m_Precomputed.reset();
        return;
    }
    m_impl->m_Precomputed = std::move(analysis);
}

std::optional<an::ui::ViewPresentationSettings> ImageViewWidget::framePresentationSettings() const
{
    if (!m_image_view_control || !isVisible() || m_impl->m_ImageSize.isEmpty() || m_impl->m_IsInRotationPreview ||
        (m_impl->m_IsAlternateImageRenderEnabled && m_image_view_control->hasAlternateImage()))
    {
        return std::nullopt;
    }
    // A mask layer is presented with the image, over the same part of it, by the view's own presentation.
    if (m_image_view_control->isMasked() && m_image_view_control->isShowMaskEnabled() &&
        m_image_view_control->getCurrentMask() != nullptr)
    {
        return std::nullopt;
    }
    const an::ui::ImageDocument& document = m_image_view_control->document();
    if (!document.valid() || !document.isOpen())
    {
        return std::nullopt;
    }
    an::ui::ViewPresentationSettings settings;
    settings.request = presentationRequest();
    settings.channel = m_impl->m_Channel;
    const an::ui::ViewDisplayGeometry geometry = displayGeometry();
    settings.proxyFactor = geometry.proxyFactor;
    settings.region = geometry.region;
    return settings;
}

bool ImageViewWidget::adoptPresentation(const an::ui::ViewPresentationSettings& settings,
                                        const an::ui::ViewPresentationOutcome& outcome,
                                        std::shared_ptr<const an::ui::DisplayProxy> proxy)
{
    const std::chrono::steady_clock::time_point adoptStart = std::chrono::steady_clock::now();
    const std::optional<an::ui::ViewPresentationSettings> now = framePresentationSettings();
    const std::string presentationPolicy = an::ui::viewPresentationKey(settings);
    if (!now.has_value() || an::ui::viewPresentationKey(*now) != presentationPolicy || !outcome.image)
    {
        return false;
    }
    // Filed as the view's own presentation of these settings would be, so a request for them presents nothing again,
    // and one on its way for what this replaces is not painted over it.
    const an::ui::ImageDocument& document = m_image_view_control->document();
    const std::string channelKey = an::ui::viewChannelKey(settings.channel);
    m_impl->m_Presentations.cancel();
    m_impl->m_InFlightKey.reset();
    m_impl->m_InFlightRequest = 0u;
    m_impl->m_Proxies.keep(std::move(proxy));
    m_impl->m_Presented = outcome.image;
    m_impl->m_PresentedImage = an::ui::presentedQImage(outcome.image);
    m_impl->m_PresentedKey = an::ui::derivedResultKey(document.revision(), {}, channelKey, {}, presentationPolicy);
    m_impl->m_PresentedFactor = outcome.factor;
    m_impl->m_PresentedCover = outcome.covered;
    m_impl->m_PresentedAnalysis = outcome.analysis;
    m_impl->m_ImageSize = QSize(static_cast<int>(outcome.sourceWidth), static_cast<int>(outcome.sourceHeight));
    m_impl->m_PaintsAdopted = true;
    keepAnalysis(an::ui::derivedResultKey(document.revision(), {}, channelKey, analysisPolicyOf(settings.request)),
                 outcome.analysis);
    m_impl->m_MaskLayer.reset();
    m_impl->m_MaskImage = QImage();
    clearPresentationLayer();
    ++m_impl->m_Telemetry.adopted;
    m_impl->m_Telemetry.presentedFactor = outcome.factor;

    updateImageScale();
    update();
    const std::chrono::nanoseconds adopted = elapsedSince(adoptStart);
    m_impl->m_Telemetry.adoptTime += adopted;
    m_impl->m_Telemetry.longestAdopt = (std::max)(m_impl->m_Telemetry.longestAdopt, adopted);
    return true;
}

bool ImageViewWidget::paintsDocumentAs(const an::ui::ViewPresentationSettings& settings) const
{
    const std::optional<an::ui::ViewPresentationSettings> now = framePresentationSettings();
    const std::string presentationPolicy = an::ui::viewPresentationKey(settings);
    if (!now.has_value() || an::ui::viewPresentationKey(*now) != presentationPolicy || !m_impl->m_Presented ||
        m_impl->m_InFlightKey.has_value())
    {
        return false;
    }
    // Filed as the view's own presentation of the document under these settings is, or as a frame's adopted one.
    return m_impl->m_PresentedKey == an::ui::derivedResultKey(m_image_view_control->document().revision(), {},
                                                              an::ui::viewChannelKey(settings.channel), {},
                                                              presentationPolicy);
}

bool ImageViewWidget::paintsDocumentOver(const an::ui::ViewPresentationSettings& settings, const QRect& part) const
{
    if (!m_image_view_control || !m_impl->m_Presented || m_impl->m_InFlightKey.has_value() || part.isEmpty() ||
        m_impl->m_PresentedFactor != (std::max)(1u, settings.proxyFactor) || !framePresentationSettings().has_value())
    {
        return false;
    }
    const an::ui::ViewRegion& cover = m_impl->m_PresentedCover;
    const QRect painted(static_cast<int>(cover.x), static_cast<int>(cover.y), static_cast<int>(cover.width),
                        static_cast<int>(cover.height));
    if (!painted.contains(part))
    {
        return false;
    }
    // Filed as the view's own presentation of the document under these settings, made for the region it covers: none
    // when it covers every block of the image's reduction, as a request for the whole image is made.
    const std::uint32_t factor = m_impl->m_PresentedFactor;
    const auto blocksWide = static_cast<std::uint32_t>((std::max)(0, m_impl->m_ImageSize.width())) / factor * factor;
    const auto blocksHigh = static_cast<std::uint32_t>((std::max)(0, m_impl->m_ImageSize.height())) / factor * factor;
    an::ui::ViewPresentationSettings asPainted = settings;
    asPainted.region.reset();
    if (cover.x != 0u || cover.y != 0u || cover.width < blocksWide || cover.height < blocksHigh)
    {
        asPainted.region = cover;
    }
    return m_impl->m_PresentedKey == an::ui::derivedResultKey(m_image_view_control->document().revision(), {},
                                                              an::ui::viewChannelKey(settings.channel), {},
                                                              an::ui::viewPresentationKey(asPainted));
}

bool ImageViewWidget::adoptPresentationLayer(const an::ui::ViewPresentationSettings& settings,
                                             const an::ui::ViewPresentationOutcome& outcome)
{
    const std::chrono::steady_clock::time_point adoptStart = std::chrono::steady_clock::now();
    const an::ui::ViewRegion& part = outcome.covered;
    if (!outcome.image ||
        !paintsDocumentOver(settings, QRect(static_cast<int>(part.x), static_cast<int>(part.y),
                                            static_cast<int>(part.width), static_cast<int>(part.height))))
    {
        return false;
    }
    m_impl->m_Layer = outcome.image;
    m_impl->m_LayerImage = an::ui::presentedQImage(outcome.image);
    m_impl->m_LayerCover = outcome.covered;
    m_impl->m_LayerFactor = (std::max)(1u, outcome.factor);
    ++m_impl->m_Telemetry.layersAdopted;
    update();
    const std::chrono::nanoseconds adopted = elapsedSince(adoptStart);
    m_impl->m_Telemetry.adoptTime += adopted;
    m_impl->m_Telemetry.longestAdopt = (std::max)(m_impl->m_Telemetry.longestAdopt, adopted);
    return true;
}

std::shared_ptr<const an::ui::PresentedImage> ImageViewWidget::presentationLayer() const
{
    return m_impl->m_Layer;
}

QRect ImageViewWidget::presentationLayerRegion() const
{
    if (!m_impl->m_Layer)
    {
        return {};
    }
    const an::ui::ViewRegion& cover = m_impl->m_LayerCover;
    return QRect(static_cast<int>(cover.x), static_cast<int>(cover.y), static_cast<int>(cover.width),
                 static_cast<int>(cover.height));
}

void ImageViewWidget::clearPresentationLayer()
{
    if (!m_impl->m_Layer)
    {
        return;
    }
    m_impl->m_Layer.reset();
    m_impl->m_LayerImage = QImage();
    m_impl->m_LayerCover = {};
    m_impl->m_LayerFactor = 1u;
    update();
}

ImageViewPresentationTelemetry ImageViewWidget::presentationTelemetry() const
{
    return m_impl->m_Telemetry;
}

bool ImageViewWidget::isPresentationIdle() const
{
    return m_impl->m_Outstanding->load(std::memory_order_acquire) == 0u;
}

void ImageViewWidget::keepAnalysis(const an::ui::DerivedResultKey& key, const an::DisplayPresentationAnalysis& analysis)
{
    if (analysis.realtimePreview || !m_impl->m_AutostretchStatisticsCachingEnabled)
    {
        return;
    }
    // A linear presentation measures nothing: its analysis never replaces one measured under the same key, so a view
    // that leaves autostretch and comes back presents through what it measured rather than measuring the whole image
    // again.
    if (!analysis.measured && m_impl->m_Analysis.has_value() && m_impl->m_Analysis->key == key &&
        m_impl->m_Analysis->analysis.measured)
    {
        return;
    }
    m_impl->m_Analysis = Impl::KeptAnalysis{key, analysis};
}

void ImageViewWidget::presentDocumentAgain()
{
    if (!m_impl->m_PaintsAdopted && !m_impl->m_Layer)
    {
        return;
    }
    // The frame's bytes stay painted until the document's own presentation lands; what was filed under the view's key
    // is forgotten so that the request is not taken for one already answered.
    clearPresentationLayer();
    m_impl->m_PaintsAdopted = false;
    m_impl->m_PresentedKey = {};
    requestPresentation();
}

void ImageViewWidget::clearPresentation()
{
    m_impl->m_Presentations.cancel();
    m_impl->m_Presented.reset();
    m_impl->m_PresentedImage = QImage();
    m_impl->m_PresentedKey = {};
    m_impl->m_PresentedCover = {};
    m_impl->m_PresentedAnalysis.reset();
    m_impl->m_ImageSize = QSize();
    m_impl->m_PaintsAdopted = false;
    m_impl->m_Analysis.reset();
    m_impl->m_MaskLayer.reset();
    m_impl->m_MaskImage = QImage();
    clearPresentationLayer();
    m_impl->m_Proxies.clear();
    m_impl->m_MaskProxies.clear();
    update();
}

void ImageViewWidget::requestPresentation()
{
    if (!m_image_view_control)
    {
        return;
    }
    watchViewport();
    struct Requesting
    {
        bool& flag;
        bool was;
        ~Requesting()
        {
            flag = was;
        }
    } requesting{m_impl->m_Requesting, std::exchange(m_impl->m_Requesting, true)};
    const an::consumer::EngineSession* session = viewSession();
    const bool alternate = m_impl->m_IsAlternateImageRenderEnabled && m_image_view_control->hasAlternateImage();

    PresentationJob job;
    an::ui::DocumentRevision revision;
    const an::ui::ImageDocument& document = m_image_view_control->document();
    if (alternate)
    {
        job.asset = m_image_view_control->alternateAsset();
        if (!job.asset)
        {
            clearPresentation();
            return;
        }
        revision.asset = job.asset->revision();
    }
    else
    {
        if (!document.valid() || !document.isOpen())
        {
            clearPresentation();
            return;
        }
        job.asset = document.asset();
        revision = document.revision();
    }
    if (const std::optional<an::ImageDescriptor> described = job.asset->descriptor(); described.has_value())
    {
        const QSize size(static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height()));
        if (size != m_impl->m_ImageSize)
        {
            // The widget takes the new image's size now, so what it shows - and so what is presented - is the new
            // one's.
            m_impl->m_ImageSize = size;
            updateImageScale();
        }
    }

    // What the view shows, decided now: the stretch, the channel, and the reduction and region it paints.
    an::DisplayPresentationRequest& request = job.settings.request;
    request = presentationRequest();
    job.settings.channel = m_impl->m_Channel;
    const an::ui::ViewDisplayGeometry geometry = displayGeometry();
    job.settings.proxyFactor = geometry.proxyFactor;
    job.settings.region = geometry.region;

    // The mask shown over the image is presented with it, over the same part at the same reduction.
    std::vector<an::consumer::AssetRevision> maskRevisions;
    std::string maskParameters;
    ImageViewControl* maskView = m_image_view_control->getCurrentMask();
    if (m_image_view_control->isMasked() && m_image_view_control->isShowMaskEnabled() && maskView != nullptr)
    {
        const an::ui::ImageDocument& maskDocument = maskView->document();
        if (maskDocument.valid() && maskDocument.isOpen())
        {
            job.mask = maskDocument.asset();
            job.maskSettings =
                an::ui::maskOverlaySettingsFor(m_image_view_control->getMaskRenderingMode(),
                                               m_image_view_control->isMaskInverted(), m_impl->m_Channel);
            job.maskSettings.proxyFactor = geometry.proxyFactor;
            job.maskSettings.region = geometry.region;
            maskRevisions.push_back(job.mask->revision());
            std::ostringstream parameters;
            parameters << "mask=" << static_cast<int>(job.maskSettings.mode)
                       << ";tint=" << int{job.maskSettings.tint.red} << ',' << int{job.maskSettings.tint.green} << ','
                       << int{job.maskSettings.tint.blue} << ";invert=" << job.maskSettings.invert;
            maskParameters = parameters.str();
        }
    }

    const std::string channelKey = an::ui::viewChannelKey(job.settings.channel);
    const std::string presentationPolicy = an::ui::viewPresentationKey(job.settings);
    const std::string analysisPolicy = analysisPolicyOf(request);
    job.key = an::ui::derivedResultKey(revision, maskRevisions, channelKey, maskParameters, presentationPolicy);
    job.analysisKey = an::ui::derivedResultKey(revision, {}, channelKey, analysisPolicy);
    ++m_impl->m_Telemetry.requested;

    if (m_impl->m_Presented && job.key == m_impl->m_PresentedKey)
    {
        // What is painted is what was asked for: nothing to present, and nothing on its way for what the view showed
        // before is painted over it.
        if (m_impl->m_InFlightKey.has_value())
        {
            m_impl->m_Presentations.cancel();
            m_impl->m_InFlightKey.reset();
            m_impl->m_InFlightRequest = 0u;
        }
        ++m_impl->m_Telemetry.committed;
        updateImageScale();
        update();
        return;
    }
    if (m_impl->m_InFlightKey.has_value() && *m_impl->m_InFlightKey == job.key &&
        m_impl->m_Presentations.currentRequest() == m_impl->m_InFlightRequest && !m_impl->m_Precomputed.has_value())
    {
        // The same presentation is on its way and still current: it paints when it lands.
        ++m_impl->m_Telemetry.coalesced;
        return;
    }

    // With no session lent nothing is presented, and nothing is left on its way either: a later request for the same
    // key, once a session is lent, is not taken for one already answered.
    if (session == nullptr)
    {
        LOG_WARNING("Image view: no engine session is lent to present '{}'", m_image_view_control->getUniqueName());
        ++m_impl->m_Telemetry.failed;
        return;
    }

    // The ticket is issued only now, so a request that presents nothing new never supersedes one that does.
    if (alternate)
    {
        job.ticket = m_impl->m_Presentations.issueUnbound();
    }
    else
    {
        an::Result<an::ui::CommitTicket> issued = m_impl->m_Presentations.issue(document);
        if (!issued.hasValue())
        {
            clearPresentation();
            return;
        }
        job.ticket = std::move(*issued);
        if (job.ticket.source() != revision)
        {
            // A writer let go between the two reads: the request presents what the ticket captured.
            revision = job.ticket.source();
            job.asset = document.asset();
            job.key = an::ui::derivedResultKey(revision, maskRevisions, channelKey, maskParameters, presentationPolicy);
            job.analysisKey = an::ui::derivedResultKey(revision, {}, channelKey, analysisPolicy);
        }
    }
    m_impl->m_InFlightKey = job.key;
    m_impl->m_InFlightRequest = job.ticket.requestId();
    m_impl->m_InFlightGeometry = geometry;
    if (m_impl->m_Precomputed.has_value())
    {
        job.reuse = std::move(m_impl->m_Precomputed);
        m_impl->m_Precomputed.reset();
    }
    else if (m_impl->m_AutostretchStatisticsCachingEnabled && m_impl->m_Analysis.has_value() &&
             m_impl->m_Analysis->key == job.analysisKey)
    {
        job.reuse = m_impl->m_Analysis->analysis;
    }
    if (geometry.proxyFactor > 1u)
    {
        job.proxy = m_impl->m_Proxies.find(revision.asset, geometry.proxyFactor);
        if (job.mask)
        {
            job.maskProxy = m_impl->m_MaskProxies.find(maskRevisions.front(), geometry.proxyFactor);
        }
    }
    auto shared = std::make_shared<const PresentationJob>(std::move(job));
    const std::shared_ptr<std::atomic<std::uint32_t>> outstanding = m_impl->m_Outstanding;
    outstanding->fetch_add(1u, std::memory_order_acq_rel);
    QPointer<ImageViewWidget> guard(this);
    QThreadPool::globalInstance()->start([guard, shared, session, outstanding]() {
        auto result = std::make_shared<PresentationResult>();
        {
            // Every lease before the admission, in one order: the view's edit, or an edit of the mask's view, holds its
            // writer while it waits to be admitted, and nothing stops a presentation while an edit runs.
            an::ui::ImageReads reads;
            reads.name(shared->asset);
            reads.name(shared->mask);
            const an::Status leased = reads.take(shared->ticket.stopToken());
            if (leased.failed())
            {
                result->outcome = leased;
            }
            else
            {
                an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                    .purpose = an::consumer::OperationPurpose::Preview, .stopToken = shared->ticket.stopToken()});
                if (!scope.hasValue())
                {
                    result->outcome = scope.status();
                }
                else
                {
                    const std::chrono::steady_clock::time_point begun = std::chrono::steady_clock::now();
                    // A proxy kept for the revision is presented from; one the image was written past since is not.
                    const auto proxyFor =
                        [&](const an::consumer::AssetReadLease& lease, std::shared_ptr<const an::ui::DisplayProxy> kept,
                            std::uint32_t factor) -> an::Result<std::shared_ptr<const an::ui::DisplayProxy>> {
                        if (factor <= 1u)
                        {
                            return std::shared_ptr<const an::ui::DisplayProxy>();
                        }
                        if (kept && kept->source() == lease.revision() && kept->factor() == factor)
                        {
                            return kept;
                        }
                        an::Result<std::shared_ptr<const an::ui::DisplayProxy>> reduced =
                            an::ui::DisplayProxy::build(scope->engine(), scope->execution(), lease, factor);
                        if (reduced.hasValue())
                        {
                            result->reductions.push_back((*reduced)->reduceTime());
                        }
                        return reduced;
                    };
                    const an::consumer::AssetReadLease* image = reads.find(shared->asset->identity());
                    an::Result<std::shared_ptr<const an::ui::DisplayProxy>> proxy =
                        image != nullptr ? proxyFor(*image, shared->proxy, shared->settings.proxyFactor)
                                         : an::Result<std::shared_ptr<const an::ui::DisplayProxy>>(an::Status::failure(
                                               an::ErrorCode::NotInitialized, "the view's image is under no lease"));
                    if (!proxy.hasValue())
                    {
                        result->outcome = proxy.status();
                    }
                    else
                    {
                        result->proxy = *proxy;
                        result->outcome =
                            an::ui::presentForView(scope->engine(), scope->execution(), *image, shared->settings,
                                                   shared->reuse.has_value() ? &*shared->reuse : nullptr,
                                                   &session->memory(), result->proxy.get());
                    }
                    const an::consumer::AssetReadLease* mask =
                        shared->mask ? reads.find(shared->mask->identity()) : nullptr;
                    if (result->outcome.hasValue() && mask != nullptr)
                    {
                        an::Result<std::shared_ptr<const an::ui::DisplayProxy>> maskProxy =
                            proxyFor(*mask, shared->maskProxy, shared->maskSettings.proxyFactor);
                        if (!maskProxy.hasValue())
                        {
                            result->maskLayer = maskProxy.status();
                        }
                        else
                        {
                            result->maskProxy = *maskProxy;
                            const bool multiply = shared->maskSettings.mode == an::MaskOverlayMode::Multiply;
                            result->maskLayer = an::ui::renderViewMaskOverlay(
                                scope->engine(), scope->execution(), *mask, shared->maskSettings,
                                multiply ? result->outcome->image.get() : nullptr, &session->memory(),
                                result->maskProxy.get());
                        }
                        if (result->maskLayer.hasValue())
                        {
                            // Premultiplied here, so the GUI thread paints the layer without converting it.
                            result->maskImage = an::ui::presentedQImage(*result->maskLayer)
                                                    .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                        }
                    }
                    result->presentTime = elapsedSince(begun);
                }
            }
        }
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            outstanding->fetch_sub(1u, std::memory_order_acq_rel);
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, shared, result, outstanding]() {
                outstanding->fetch_sub(1u, std::memory_order_acq_rel);
                if (guard)
                {
                    guard->commitPresentation(*shared, *result);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageViewWidget::commitPresentation(const PresentationJob& job, const PresentationResult& result)
{
    const std::chrono::steady_clock::time_point adoptStart = std::chrono::steady_clock::now();
    if (result.presentTime.has_value())
    {
        ++m_impl->m_Telemetry.presented;
        m_impl->m_Telemetry.presentTime += *result.presentTime;
        m_impl->m_Telemetry.longestPresent = (std::max)(m_impl->m_Telemetry.longestPresent, *result.presentTime);
    }
    for (const std::chrono::nanoseconds reduced : result.reductions)
    {
        ++m_impl->m_Telemetry.proxiesReduced;
        m_impl->m_Telemetry.reduceTime += reduced;
        m_impl->m_Telemetry.longestReduce = (std::max)(m_impl->m_Telemetry.longestReduce, reduced);
    }
    // A proxy is immutable and keyed by what it was reduced from, so it is kept whatever becomes of the presentation:
    // a later one of the same revision and factor reduces nothing again.
    m_impl->m_Proxies.keep(result.proxy);
    m_impl->m_MaskProxies.keep(result.maskProxy);
    if (job.ticket.requestId() == m_impl->m_InFlightRequest)
    {
        m_impl->m_InFlightKey.reset();
        m_impl->m_InFlightRequest = 0u;
    }
    const an::ui::CommitVerdict verdict = job.ticket.verdict();
    if (verdict != an::ui::CommitVerdict::Commit)
    {
        // A presentation of a state that is no longer the view's - superseded, cancelled, closed or changed - is
        // discarded: a newer request is on its way, or there is nothing to show.
        ++m_impl->m_Telemetry.discarded;
        if (job.mask)
        {
            ++m_impl->m_Telemetry.masksDiscarded;
        }
        return;
    }
    if (!result.outcome.hasValue())
    {
        ++m_impl->m_Telemetry.failed;
        if (result.outcome.status().code() != an::ErrorCode::Cancelled)
        {
            LOG_WARNING("Image view: '{}' could not be presented: {}",
                        m_image_view_control ? m_image_view_control->getUniqueName() : std::string(),
                        result.outcome.status().message());
        }
        return;
    }
    const an::ui::ViewPresentationOutcome& presented = *result.outcome;
    // What a frame painted of a part of the document goes with the presentation it was painted over.
    clearPresentationLayer();
    m_impl->m_Presented = presented.image;
    m_impl->m_PresentedImage = an::ui::presentedQImage(presented.image);
    m_impl->m_PresentedKey = job.key;
    m_impl->m_PresentedFactor = presented.factor;
    m_impl->m_PresentedCover = presented.covered;
    m_impl->m_PresentedAnalysis = presented.analysis;
    m_impl->m_ImageSize = QSize(static_cast<int>(presented.sourceWidth), static_cast<int>(presented.sourceHeight));
    m_impl->m_PaintsAdopted = false;
    keepAnalysis(job.analysisKey, presented.analysis);
    if (presented.measuredWhole)
    {
        ++m_impl->m_Telemetry.wholeImageMeasured;
        m_impl->m_Telemetry.wholeImageMeasureTime += presented.wholeMeasureTime;
    }
    if (!job.mask)
    {
        m_impl->m_MaskLayer.reset();
        m_impl->m_MaskImage = QImage();
    }
    else if (!result.maskLayer.hasValue())
    {
        LOG_WARNING("Image view: the mask of '{}' could not be shown: {}",
                    m_image_view_control ? m_image_view_control->getUniqueName() : std::string(),
                    result.maskLayer.status().message());
        m_impl->m_MaskLayer.reset();
        m_impl->m_MaskImage = QImage();
    }
    else
    {
        m_impl->m_MaskLayer = *result.maskLayer;
        m_impl->m_MaskImage = result.maskImage;
        ++m_impl->m_Telemetry.masksCommitted;
    }
    ++m_impl->m_Telemetry.committed;
    m_impl->m_Telemetry.presentedFactor = presented.factor;

    updateImageScale();
    update();
    const std::chrono::nanoseconds adopted = elapsedSince(adoptStart);
    m_impl->m_Telemetry.adoptTime += adopted;
    m_impl->m_Telemetry.longestAdopt = (std::max)(m_impl->m_Telemetry.longestAdopt, adopted);
}

void ImageViewWidget::requestMaskOverlay()
{
    if (!m_image_view_control)
    {
        return;
    }
    // The layer is presented with the image, so a mask shown, hidden, inverted or edited is a new presentation of
    // both; a layer no longer shown goes at once.
    if (!m_image_view_control->isMasked() || !m_image_view_control->isShowMaskEnabled() ||
        m_image_view_control->getCurrentMask() == nullptr)
    {
        m_impl->m_MaskLayer.reset();
        m_impl->m_MaskImage = QImage();
        update();
    }
    requestPresentation();
}

std::optional<an::ui::ViewRegion> ImageViewWidget::shownImageRegion() const
{
    // A view nobody sees shows nothing of its image, and is presented whole.
    const double scale = m_impl->m_CurrentScale;
    if (!m_image_view_control || !isVisible() || m_impl->m_ImageSize.isEmpty() || !std::isfinite(scale) || scale <= 0.0)
    {
        return std::nullopt;
    }
    const ImageViewScrollArea* area = m_image_view_control->getScrollArea();
    const QWidget* viewport = area != nullptr ? area->viewport() : nullptr;
    if (viewport == nullptr || viewport->width() <= 0 || viewport->height() <= 0)
    {
        return std::nullopt;
    }
    const QRect visible = QRect(mapFrom(viewport, QPoint(0, 0)), viewport->size()).intersected(rect());
    if (visible.isEmpty())
    {
        return std::nullopt;
    }
    const auto width = static_cast<std::int64_t>(m_impl->m_ImageSize.width());
    const auto height = static_cast<std::int64_t>(m_impl->m_ImageSize.height());
    const std::int64_t left =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(visible.left() / scale)), 0, width - 1);
    const std::int64_t top =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(visible.top() / scale)), 0, height - 1);
    const std::int64_t right =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil((visible.right() + 1) / scale)), left + 1, width);
    const std::int64_t bottom =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil((visible.bottom() + 1) / scale)), top + 1, height);
    return an::ui::ViewRegion{.x = static_cast<std::uint32_t>(left),
                              .y = static_cast<std::uint32_t>(top),
                              .width = static_cast<std::uint32_t>(right - left),
                              .height = static_cast<std::uint32_t>(bottom - top)};
}

an::ui::ViewDisplayGeometry ImageViewWidget::displayGeometry() const
{
    // A rotation preview paints the whole image turned, so it is presented whole.
    const std::optional<an::ui::ViewRegion> shown =
        m_impl->m_IsInRotationPreview ? std::optional<an::ui::ViewRegion>{} : shownImageRegion();
    return an::ui::viewDisplayGeometry(static_cast<std::uint32_t>((std::max)(0, m_impl->m_ImageSize.width())),
                                       static_cast<std::uint32_t>((std::max)(0, m_impl->m_ImageSize.height())),
                                       m_impl->m_CurrentScale, devicePixelRatioF(), shown);
}

void ImageViewWidget::onViewportMoved()
{
    // A request being decided resizes the widget for the image it presents, and decides what to present after that.
    if (m_impl->m_Requesting || m_impl->m_IsInRotationPreview ||
        (!m_impl->m_Presented && !m_impl->m_InFlightKey.has_value()))
    {
        return;
    }
    const std::optional<an::ui::ViewRegion> shown = shownImageRegion();
    if (!shown.has_value())
    {
        return;
    }
    const an::ui::ViewDisplayGeometry wanted = displayGeometry();
    const QSize image = m_impl->m_ImageSize;
    // A presentation on its way is painted over what is painted now when it lands, so it is the one that must hold
    // what is shown: a zoom asks for one before its scroll bars move the view, and what is painted may still hold the
    // part the view moved to while what is on its way does not.
    if (m_impl->m_InFlightKey.has_value())
    {
        if (m_impl->m_InFlightGeometry.proxyFactor == wanted.proxyFactor &&
            holds(coverOf(m_impl->m_InFlightGeometry, image), wanted.proxyFactor, *shown, image))
        {
            return;
        }
    }
    else if (m_impl->m_Presented && m_impl->m_PresentedFactor == wanted.proxyFactor &&
             holds(m_impl->m_PresentedCover, wanted.proxyFactor, *shown, image))
    {
        return;
    }
    // What is shown now lies beyond what is painted and what is on its way: present the part around it.
    requestPresentation();
}

void ImageViewWidget::watchViewport()
{
    if (m_impl->m_WatchingViewport || !m_image_view_control)
    {
        return;
    }
    ImageViewScrollArea* area = m_image_view_control->getScrollArea();
    if (area == nullptr || area->viewport() == nullptr)
    {
        return;
    }
    m_impl->m_WatchingViewport = true;
    connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { onViewportMoved(); });
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { onViewportMoved(); });
    area->viewport()->installEventFilter(this);
}

bool ImageViewWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize && m_image_view_control != nullptr &&
        m_image_view_control->getScrollArea() != nullptr &&
        watched == m_image_view_control->getScrollArea()->viewport())
    {
        onViewportMoved();
    }
    return QWidget::eventFilter(watched, event);
}

ImageViewWidget::~ImageViewWidget()
{
    m_impl->m_Presentations.cancel();
    m_impl->m_ImageLensWidget->detachFromOwner();

    auto tmpDisplayModules = m_impl->m_DisplayModules;
    for (auto& module : tmpDisplayModules)
    {
        module->detachFromViewWidget();
    }
}

void ImageViewWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(this->rect(), an::ui::tokens().colors.bgPrimary);

    const QPoint center = this->rect().center() + QPoint(1, 1);
    const bool rotationPreviewActive = m_impl->m_IsInRotationPreview && m_impl->m_RotationPreviewAngle != 0.0;
    const double scale = m_impl->m_CurrentScale;

    // The presented bytes cover a part of the image at a reduction; they are drawn where that part lies at the zoom,
    // smoothed when they are reduced on the way to the screen and as blocks of native pixels when magnified. A rotation
    // preview centres the image on the widget.
    auto drawImageLayers = [&]() {
        const QSize painted = getRenderImageSize();
        const QPointF origin = rotationPreviewActive
                                   ? QPointF(center) - QPointF(painted.width() / 2.0, painted.height() / 2.0)
                                   : QPointF(0.0, 0.0);
        // The image is laid over the size it is painted at, which the zoom rounded to whole pixels, so its last row and
        // column end with the widget's.
        const auto imageWidth = static_cast<std::uint64_t>((std::max)(0, m_impl->m_ImageSize.width()));
        const auto imageHeight = static_cast<std::uint64_t>((std::max)(0, m_impl->m_ImageSize.height()));
        const double toX = imageWidth > 0u ? painted.width() / static_cast<double>(imageWidth) : scale;
        const double toY = imageHeight > 0u ? painted.height() / static_cast<double>(imageHeight) : scale;
        // Unturned, the image's edges are drawn half a device pixel beyond the widget's: a pixel the widget covers by a
        // fraction is then the image's whichever way that fraction is rounded, and the widget's clip keeps what lies
        // beyond.
        const QTransform device = painter.deviceTransform();
        const double bleedX = !rotationPreviewActive && device.m11() > 0.0 ? 0.5 / device.m11() : 0.0;
        const double bleedY = !rotationPreviewActive && device.m22() > 0.0 ? 0.5 / device.m22() : 0.0;
        // A part presented at a reduction covers whole blocks of it; the strip at the image's right and bottom edges
        // that no whole block covers, narrower than a pixel of the screen, is painted with the blocks beside it.
        const auto targetOf = [&](const an::ui::ViewRegion& cover, std::uint32_t factor) {
            const auto edge = [factor](std::uint64_t end, std::uint64_t size) {
                return end < size && end + factor > size ? size : end;
            };
            const std::uint64_t right = edge(std::uint64_t{cover.x} + cover.width, imageWidth);
            const std::uint64_t bottom = edge(std::uint64_t{cover.y} + cover.height, imageHeight);
            return QRectF(QPointF(origin.x() + (cover.x == 0u ? -bleedX : cover.x * toX),
                                  origin.y() + (cover.y == 0u ? -bleedY : cover.y * toY)),
                          QPointF(origin.x() + (right == imageWidth ? painted.width() + bleedX : right * toX),
                                  origin.y() + (bottom == imageHeight ? painted.height() + bleedY : bottom * toY)));
        };
        const QRectF target = targetOf(m_impl->m_PresentedCover, m_impl->m_PresentedFactor);
        const double devicePixelsPerPresented =
            static_cast<double>(m_impl->m_PresentedFactor) * scale * devicePixelRatioF();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, devicePixelsPerPresented < 1.0);
        const auto drawLayer = [&](const QImage& layer, const QRectF& where) {
            if (!layer.isNull())
            {
                painter.drawImage(where, layer, QRectF(0.0, 0.0, layer.width(), layer.height()));
            }
        };
        drawLayer(m_impl->m_PresentedImage, target);
        // A frame's part of the document over the presentation, where that part lies, at its own reduction.
        if (!m_impl->m_LayerImage.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                  static_cast<double>(m_impl->m_LayerFactor) * scale * devicePixelRatioF() < 1.0);
            drawLayer(m_impl->m_LayerImage, targetOf(m_impl->m_LayerCover, m_impl->m_LayerFactor));
            painter.setRenderHint(QPainter::SmoothPixmapTransform, devicePixelsPerPresented < 1.0);
        }
        drawLayer(m_impl->m_MaskImage, target);
    };

    if (rotationPreviewActive)
    {
        an::ui::PainterGuard painterGuard(&painter);
        painter.fillRect(this->rect(), an::ui::tokens().colors.bgPrimary);
        painter.translate(center);
        painter.rotate(m_impl->m_RotationPreviewAngle);
        painter.translate(-center);
        drawImageLayers();
    }
    else
    {
        drawImageLayers();
    }

    // Draw modules
    for (auto& module : m_impl->m_DisplayModules)
    {
        if (module->isDisplayModuleEnabled())
        {
            module->onDraw(painter);
        }
    }

    if (m_impl->m_IsSelectionAreaEnabled)
    {
        drawSelectionArea(painter);
    }
}

void ImageViewWidget::mousePressEvent(QMouseEvent* ev)
{
    if (!m_impl->m_QuickZoomEnabled || ev->button() == Qt::MiddleButton)
    {
        QWidget::mousePressEvent(ev);
    }

    if (m_image_view_control->getViewControlMode() == ViewControlMode::standard)
    {
        m_image_view_control->setActiveControl();
    }

    if (m_impl->m_QuickZoomEnabled && ev->button() == Qt::LeftButton)
    {
        m_impl->m_MouseLeftButtonPressed = true;

        if (m_image_view_control->isSequence())
        {
            m_image_view_control->pauseSequence();
        }

        updateQuickZoomWidget(ev->pos(), m_impl->m_IsAlternateImageForQuickZoomRenderEnabled);
        m_impl->m_QuickZoomTimer->start(m_impl->m_QuickZoomDelayMs); // Adjust the delay as needed
    }
    else if (ev->button() == Qt::RightButton)
    {
        if (m_image_view_control->isSequence())
        {
            m_image_view_control->pauseSequence();
        }

        if (m_image_view_control->getViewControlMode() == ViewControlMode::rt_preview)
        {
            enableAlternateImageRendering(!m_impl->m_IsAlternateImageRenderEnabled);
        }

        if (m_impl->m_MouseLeftButtonPressed && m_impl->m_QuickZoomEnabled)
        {
            m_impl->m_IsAlternateImageForQuickZoomRenderEnabled = true;
            updateQuickZoomWidget(mapFromGlobal(QCursor::pos()), true);
        }
    }
}

void ImageViewWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (!m_impl->m_QuickZoomEnabled || ev->button() == Qt::MiddleButton)
    {
        QWidget::mousePressEvent(ev);
    }

    if (m_impl->m_QuickZoomEnabled && ev->button() == Qt::LeftButton)
    {
        m_impl->m_MouseLeftButtonPressed = false;
        m_impl->m_QuickZoomTimer->stop(); // Stop the timer if the button is released
        m_impl->m_ImageLensWidget->hide();
        m_impl->m_ImageLensWidget->detachFromOwner();

        m_image_view_control->getScrollArea()->restoreDefaultCursor();
    }
    else if (ev->button() == Qt::RightButton)
    {
        if (m_image_view_control->getViewControlMode() == ViewControlMode::rt_preview)
        {
            enableAlternateImageRendering(!m_impl->m_IsAlternateImageRenderEnabled);
        }

        if (m_impl->m_MouseLeftButtonPressed && m_impl->m_QuickZoomEnabled)
        {
            m_impl->m_IsAlternateImageForQuickZoomRenderEnabled = false;
            updateQuickZoomWidget(ev->pos(), false);
        }
    }
}

void ImageViewWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_impl->m_QuickZoomEnabled && m_impl->m_MouseLeftButtonPressed)
    {
        ev->accept();

        if (!this->rect().contains(this->mapFromGlobal(QCursor::pos())))
        {
            m_impl->m_ImageLensWidget->hide();
            m_image_view_control->getScrollArea()->setCursor(Qt::ArrowCursor);
        }
        else
        {
            updateQuickZoomWidget(ev->pos(), m_impl->m_IsAlternateImageForQuickZoomRenderEnabled);
        }
    }

    const QPointF image_pixel = mapWidgetPointToImagePixel(ev->pos());
    if (image_pixel.x() >= 0.0 && image_pixel.y() >= 0.0)
    {
        m_image_view_control->updateHoveredCelestialCoordsForImagePixel(image_pixel.x(), image_pixel.y());
    }
    else
    {
        m_image_view_control->clearHoveredCelestialCoords();
    }

    QWidget::mouseMoveEvent(ev);
}

void ImageViewWidget::drawSelectionArea(QPainter& painter)
{
    const auto& colors = an::ui::tokens().colors;
    bool drawMiddleCross = false;
    bool drawText = false;
    QPen pen = painter.pen();
    if (m_impl->m_ImageViewSelectionStyle == ImageViewSelectionStyle::CROP_STYLE)
    {
        drawMiddleCross = true;

        pen.setStyle(Qt::DashLine);
        pen.setColor(colors.statusWarning);
        pen.setWidthF(1.5f);
        pen.setCosmetic(true);
    }
    else if (m_impl->m_ImageViewSelectionStyle == ImageViewSelectionStyle::AREA_SELECTION_STYLE)
    {
        pen.setStyle(Qt::DashLine);
        pen.setColor(colors.accentPrimary);
        pen.setWidth(1);
        pen.setCosmetic(true);
    }
    else if (m_impl->m_ImageViewSelectionStyle == ImageViewSelectionStyle::PREVIEW_STYLE)
    {
        pen.setStyle(Qt::SolidLine);
        pen.setColor(colors.statusError);
        pen.setWidth(1);
        pen.setCosmetic(true);
        drawText = true;
    }

    painter.setPen(pen);

    auto rect = getCurrentScaledSelectionArea();

    if (drawText && !m_impl->m_SelectionAreaText.isEmpty())
    {
        auto pos = rect.topLeft();
        if (pos.y() < 15)
        {
            pos = rect.bottomLeft();
            pos.setY(pos.y() + 15);
        }
        else
        {
            pos.setY(pos.y() - 5);
        }

        painter.drawText(pos, m_impl->m_SelectionAreaText);
    }

    painter.drawRect(rect);

    if (drawMiddleCross)
    {
        if (rect.width() > 50 && rect.height() > 50)
        {
            // Draw quadrant grid lines (rule of thirds)
            pen.setStyle(Qt::DotLine);
            pen.setColor(Qt::yellow);
            pen.setWidthF(1.2f);
            painter.setPen(pen);

            // Draw two vertical lines at 1/3 and 2/3
            int leftThird = rect.left() + rect.width() / 3;
            int rightThird = rect.left() + (rect.width() * 2) / 3;
            painter.drawLine(leftThird, rect.top(), leftThird, rect.bottom());
            painter.drawLine(rightThird, rect.top(), rightThird, rect.bottom());

            // Draw two horizontal lines at 1/3 and 2/3
            int topThird = rect.top() + rect.height() / 3;
            int bottomThird = rect.top() + (rect.height() * 2) / 3;
            painter.drawLine(rect.left(), topThird, rect.right(), topThird);
            painter.drawLine(rect.left(), bottomThird, rect.right(), bottomThird);

            // Draw center cross for precise centering
            auto center = rect.center();
            int lineSize = int(((std::min)(rect.width(), rect.height()) * 0.03f));

            pen.setStyle(Qt::SolidLine);
            painter.setPen(pen);
            painter.drawLine(QLine(center - QPoint(lineSize, 0), center + QPoint(lineSize, 0)));
            painter.drawLine(QLine(center - QPoint(0, lineSize), center + QPoint(0, lineSize)));
        }
    }
}

void ImageViewWidget::updateImageScale()
{
    m_impl->m_CurrentScale = an::ui::ZoomManager::clampScale(m_impl->m_CurrentScale);

    const QSize scaledSize = scaleWidgetSize(sanitizeWidgetSize(m_impl->m_ImageSize), m_impl->m_CurrentScale);
    if (scaledSize.isValid() && !scaledSize.isEmpty() && this->rect().size() != scaledSize)
    {
        SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, setFixedSize, scaledSize);
    }

    checkSelectionAreaBoundaries();

    m_impl->m_ScaledSelectionArea =
        checkRectSize(scaleRect(m_impl->m_SelectionArea, m_impl->m_CurrentScale), this->rect());

    if (m_impl->m_IsInRotationPreview && m_impl->m_RotationResizeArea)
    {
        // Reset the m_impl->m_RotationPreviewAngle value so setRotationPreviewAngle() process it again
        double angle = m_impl->m_RotationPreviewAngle;
        m_impl->m_RotationPreviewAngle = 0.0;
        // Update the original rect with the new scaled area
        m_impl->m_OriginalWidgetRectForRotations = this->rect();
        // Refrersh the rotation preview
        setRotationPreviewAngle(angle);
    }
}

void ImageViewWidget::checkSelectionAreaBoundaries()
{
    QRect originalImageSize;

    if (m_impl->m_IsInRotationPreview && m_impl->m_RotationResizeArea)
    {
        originalImageSize.setSize(getCurrentRotatedAreaSize());
    }
    else
    {
        originalImageSize.setSize(m_impl->m_ImageSize);
    }

    if (originalImageSize.width() < m_impl->m_SelectionArea.width())
    {
        m_impl->m_SelectionArea.setWidth(originalImageSize.width());
    }

    if (originalImageSize.height() < m_impl->m_SelectionArea.height())
    {
        m_impl->m_SelectionArea.setHeight(originalImageSize.height());
    }
}

void ImageViewWidget::updateQuickZoomWidget(const QPoint& mousePos, const bool useOriginalimg)
{
    QWidget* parentContainer = m_image_view_control;
    if (m_image_view_control->getViewControlMode() == ViewControlMode::standard && !m_image_view_control->isMaximized())
    {
        if (QWidget* dock = m_image_view_control->getDockWidget())
        {
            parentContainer = dock;
        }
    }

    const QPoint cursorInOwner = (m_image_view_control->getViewControlMode() == ViewControlMode::standard)
                                     ? parentContainer->mapFromGlobal(QCursor::pos())
                                     : m_image_view_control->mapFromGlobal(QCursor::pos());

    QPoint newPos = an::ui::calculateQuickZoomLensPosition(cursorInOwner, m_impl->m_ImageLensWidget->size(),
                                                           parentContainer->size());

    const std::shared_ptr<const an::ui::PresentedImage> shown =
        m_impl->m_QuickZoomImageOverride ? m_impl->m_QuickZoomImageOverride : m_impl->m_Presented;
    if (!shown || m_impl->m_ImageSize.isEmpty())
    {
        return;
    }

    const auto region = an::ui::calculateQuickZoomRegion(mousePos, QPoint(0, 0), m_impl->m_CurrentScale,
                                                         m_impl->m_ImageSize.width(), m_impl->m_ImageSize.height());
    if (region.cropRect.isEmpty())
    {
        return;
    }

    // Where a part of the image lies in presented bytes: an override covers the whole image at its own size, the view's
    // presentation and its mask layer the part they cover at their reduction.
    const bool overridden = m_impl->m_QuickZoomImageOverride != nullptr;
    const an::ui::ViewRegion wholeImage{.x = 0u,
                                        .y = 0u,
                                        .width = static_cast<std::uint32_t>(m_impl->m_ImageSize.width()),
                                        .height = static_cast<std::uint32_t>(m_impl->m_ImageSize.height())};
    const auto mapInto = [](const QRect& part, const an::ui::PresentedImage& bytes, const an::ui::ViewRegion& over) {
        const double toX = static_cast<double>(bytes.width()) / static_cast<double>((std::max)(1u, over.width));
        const double toY = static_cast<double>(bytes.height()) / static_cast<double>((std::max)(1u, over.height));
        const QRect mapped(static_cast<int>(std::floor((part.x() - static_cast<double>(over.x)) * toX)),
                           static_cast<int>(std::floor((part.y() - static_cast<double>(over.y)) * toY)),
                           (std::max)(1, static_cast<int>(std::lround(part.width() * toX))),
                           (std::max)(1, static_cast<int>(std::lround(part.height() * toY))));
        return mapped.intersected(QRect(0, 0, static_cast<int>(bytes.width()), static_cast<int>(bytes.height())));
    };
    const an::ui::ViewRegion& shownCover = overridden ? wholeImage : m_impl->m_PresentedCover;
    const QRect shownRect = mapInto(region.cropRect, *shown, shownCover);

    const an::consumer::EngineSession* session = viewSession();
    std::shared_ptr<const an::ui::PresentedImage> lensBytes = shown;
    QRect lensRect = shownRect;
    if (session != nullptr && !overridden)
    {
        // The lens magnifies the image's own pixels: the region under it presented now, linearly for the original and
        // otherwise through the view's stretch and the analysis the view painted with - never measured here. Without
        // a thread free, or without that analysis, it magnifies what the view painted.
        const bool alternateShown =
            m_impl->m_IsAlternateImageRenderEnabled && m_image_view_control->hasAlternateImage();
        const std::shared_ptr<const an::consumer::ImageAsset> original =
            (useOriginalimg ? m_image_view_control->hasAlternateImage() : alternateShown)
                ? m_image_view_control->alternateAsset()
                : m_image_view_control->document().asset();
        an::ui::ViewPresentationSettings lens;
        if (useOriginalimg)
        {
            lens.request.stretch = an::PresentationStretch::Linear;
        }
        else
        {
            lens.request = presentationRequest();
        }
        lens.request.output = an::PresentationOutput::Bgra8;
        lens.channel = m_impl->m_Channel;
        lens.region = an::ui::ViewRegion{.x = static_cast<std::uint32_t>(region.cropRect.x()),
                                         .y = static_cast<std::uint32_t>(region.cropRect.y()),
                                         .width = static_cast<std::uint32_t>(region.cropRect.width()),
                                         .height = static_cast<std::uint32_t>(region.cropRect.height())};
        const bool stretched = measuresWhatItPresents(lens.request);
        const an::DisplayPresentationAnalysis* through = stretched && m_impl->m_PresentedAnalysis.has_value() &&
                                                                 m_impl->m_PresentedAnalysis->measured &&
                                                                 !m_impl->m_PresentedAnalysis->realtimePreview
                                                             ? &*m_impl->m_PresentedAnalysis
                                                             : nullptr;
        if (through != nullptr)
        {
            // Stated as the analysis was taken, so the engine accepts it and nothing is measured on this thread.
            lens.request.promotion = through->promotion;
            lens.request.neutralize = through->neutralized;
            lens.request.calibrate = through->calibrated;
        }
        if (original && (!stretched || through != nullptr))
        {
            an::Result<an::consumer::AssetReadLease> lease = original->tryRead();
            an::Result<an::consumer::OperationScope> scope = session->tryAdmit(
                an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Background});
            if (lease.hasValue() && scope.hasValue())
            {
                // Nothing is measured on this thread: an analysis the engine refuses - the document's channel count
                // changed under the painted presentation, say - leaves the lens to the bytes already painted.
                an::Result<an::ui::ViewPresentationOutcome> presented =
                    an::ui::presentForView(scope->engine(), scope->execution(), *lease, lens, through, nullptr, nullptr,
                                           an::ui::WholeMeasurement::Refused);
                if (presented.hasValue() && presented->measuredWhole)
                {
                    ++m_impl->m_Telemetry.wholeImageMeasured;
                }
                if (presented.hasValue() && !presented->measuredWhole)
                {
                    lensBytes = presented->image;
                    lensRect = QRect(0, 0, static_cast<int>(lensBytes->width()), static_cast<int>(lensBytes->height()));
                    ++m_impl->m_Telemetry.lensesOfTheImage;
                }
            }
        }
    }

    const auto lensMade = std::chrono::steady_clock::now();
    m_impl->m_ImageLensWidget->m_CurrentPixmap = QPixmap::fromImage(an::ui::presentedQImage(lensBytes).copy(lensRect));
    ++m_impl->m_Telemetry.pixmapsMade;
    m_impl->m_Telemetry.pixmapTime += std::chrono::steady_clock::now() - lensMade;

    if (session != nullptr)
    {
        // The lens never waits on the GUI thread: with no thread free, it keeps the counts it showed last.
        an::Result<an::consumer::OperationScope> scope =
            session->tryAdmit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Background});
        if (scope.hasValue())
        {
            const an::Result<an::ui::LensHistogram> counted = an::ui::countPresentedRegion(
                scope->execution(), *lensBytes, static_cast<std::uint32_t>(lensRect.x()),
                static_cast<std::uint32_t>(lensRect.y()), static_cast<std::uint32_t>(lensRect.width()),
                static_cast<std::uint32_t>(lensRect.height()));
            if (counted.hasValue())
            {
                m_impl->m_ImageLensWidget->setHistogram(*counted);
            }
        }
    }

    const double absolutePosX = region.absolutePosition.x();
    const double absolutePosY = region.absolutePosition.y();
    m_impl->m_ImageLensWidget->m_CurrentX = absolutePosX;
    m_impl->m_ImageLensWidget->m_CurrentY = absolutePosY;

    // The values under the cursor: the image's own in linear mode and for the original, what is shown otherwise.
    std::optional<an::ui::PixelReadout> readout;
    if (useOriginalimg || m_impl->m_ViewMode == ImageViewMode::LINEAR_MODE)
    {
        const std::shared_ptr<const an::consumer::ImageAsset> original =
            (useOriginalimg || m_impl->m_IsAlternateImageRenderEnabled) && m_image_view_control->hasAlternateImage()
                ? m_image_view_control->alternateAsset()
                : m_image_view_control->document().asset();
        if (original)
        {
            if (an::Result<an::consumer::AssetReadLease> lease = original->tryRead(); lease.hasValue())
            {
                readout = an::ui::readPixel(*lease, static_cast<std::int64_t>(absolutePosX),
                                            static_cast<std::int64_t>(absolutePosY));
            }
        }
    }
    if (!readout.has_value())
    {
        if (lensBytes != shown)
        {
            readout =
                an::ui::readPresentedPixel(*lensBytes, static_cast<std::int64_t>(absolutePosX) - region.cropRect.x(),
                                           static_cast<std::int64_t>(absolutePosY) - region.cropRect.y());
        }
        else
        {
            const QRect at = mapInto(QRect(static_cast<int>(absolutePosX), static_cast<int>(absolutePosY), 1, 1),
                                     *shown, shownCover);
            readout = an::ui::readPresentedPixel(*shown, at.x(), at.y());
        }
    }
    m_impl->m_ImageLensWidget->isRGB = readout->channels >= 3u;
    m_impl->m_ImageLensWidget->m_CurrentChannel0 = readout->values[0];
    m_impl->m_ImageLensWidget->m_CurrentChannel1 = readout->values[1];
    m_impl->m_ImageLensWidget->m_CurrentChannel2 = readout->values[2];

    m_impl->m_ImageLensWidget->m_CurrentMaskPixmaps.clear();
    m_impl->m_ImageLensWidget->m_CurrentMaskValues.clear();
    m_impl->m_ImageLensWidget->isMasked = false;
    if (!useOriginalimg && m_image_view_control->isMasked() && m_image_view_control->isShowMaskEnabled() &&
        m_impl->m_MaskLayer)
    {
        const auto maskMade = std::chrono::steady_clock::now();
        m_impl->m_ImageLensWidget->m_CurrentMaskPixmaps.push_back(
            QPixmap::fromImage(an::ui::presentedQImage(m_impl->m_MaskLayer)
                                   .copy(mapInto(region.cropRect, *m_impl->m_MaskLayer, m_impl->m_PresentedCover))));
        ++m_impl->m_Telemetry.pixmapsMade;
        m_impl->m_Telemetry.pixmapTime += std::chrono::steady_clock::now() - maskMade;
        if (ImageViewControl* maskView = m_image_view_control->getCurrentMask())
        {
            if (const std::shared_ptr<const an::consumer::ImageAsset> mask = maskView->document().asset())
            {
                if (an::Result<an::consumer::AssetReadLease> lease = mask->tryRead(); lease.hasValue())
                {
                    if (const std::optional<an::ui::PixelReadout> values = an::ui::readPixel(
                            *lease, static_cast<std::int64_t>(absolutePosX), static_cast<std::int64_t>(absolutePosY)))
                    {
                        for (std::uint32_t channel = 0u; channel < values->channels; ++channel)
                        {
                            m_impl->m_ImageLensWidget->m_CurrentMaskValues.push_back(values->values[channel]);
                        }
                    }
                }
            }
        }
        m_impl->m_ImageLensWidget->isMasked = !m_impl->m_ImageLensWidget->m_CurrentMaskValues.empty();
    }

    if (m_image_view_control->getViewControlMode() != ViewControlMode::standard)
    {
        newPos = m_image_view_control->mapToGlobal(newPos);
    }
    else
    {
        newPos = parentContainer->mapToGlobal(newPos);
    }

    auto func = [this, newPos, parentContainer]() {
        m_impl->m_ImageLensWidget->attachToOwner(parentContainer);
        QPoint lensPos = newPos;
        if (!m_impl->m_ImageLensWidget->isWindow() && m_impl->m_ImageLensWidget->parentWidget())
        {
            lensPos = m_impl->m_ImageLensWidget->parentWidget()->mapFromGlobal(newPos);
        }

        m_impl->m_ImageLensWidget->move(lensPos);
        m_impl->m_ImageLensWidget->update();
    };

    SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(m_impl->m_ImageLensWidget.get(), func);
}

void ImageViewWidget::showImageLensWidget()
{
    if (m_impl->m_MouseLeftButtonPressed)
    {
        m_impl->m_ImageLensWidget->show();
        m_impl->m_ImageLensWidget->raise();
    }
}

ImageViewWidgetDisplayModule::ImageViewWidgetDisplayModule() {}

ImageViewWidget* ImageViewWidgetDisplayModule::getViewWidget() const
{
    return m_ViewWidget;
}

void ImageViewWidgetDisplayModule::enableDisplayModule(const bool enabled)
{
    m_DisplayModuleEnabled = enabled;
}

bool ImageViewWidgetDisplayModule::isDisplayModuleEnabled() const
{
    return m_DisplayModuleEnabled;
}

void ImageViewWidgetDisplayModule::attachToViewWidget(ImageViewWidget* widget)
{
    detachFromViewWidget();
    m_ViewWidget = widget;
    if (m_ViewWidget)
    {
        m_ViewWidget->addDisplayModule(this);
    }
}

void ImageViewWidgetDisplayModule::detachFromViewWidget()
{
    if (m_ViewWidget)
    {
        m_ViewWidget->removeDisplayModule(this);
        m_ViewWidget = nullptr;
    }
}

ImageViewWidgetDisplayModule::~ImageViewWidgetDisplayModule()
{
    detachFromViewWidget();
}
