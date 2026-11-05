// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/ComparisonViewWidget.h"

#include "controls/ComparisonImageGenerator.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "session/CommitTicket.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ComparisonDisplayModule.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSplitter>

#include <QThreadPool>

#include <verdigris/wobjectimpl.h>

#include <cmath>

namespace
{

struct ImagePixelSize
{
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool isValid() const
    {
        return width > 0.0 && height > 0.0;
    }
};

[[nodiscard]] bool isUsablePositive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] ImagePixelSize imagePixelSize(const ImageViewControl* control)
{
    const std::optional<an::ImageDescriptor> described =
        control != nullptr ? control->imageDescriptor() : std::optional<an::ImageDescriptor>();
    if (!described.has_value())
    {
        return {};
    }

    return {static_cast<double>(described->extent().width()), static_cast<double>(described->extent().height())};
}

/// The session the comparison's work runs in, or null in a process that lends none.
[[nodiscard]] const an::consumer::EngineSession* comparisonSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

/// The overlay's picture of an image: its samples shown linearly as bytes.
[[nodiscard]] an::Result<std::shared_ptr<const an::ui::PresentedImage>>
presentOverlay(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image, std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::ui::ViewPresentationSettings settings;
    settings.request.stretch = an::PresentationStretch::Linear;
    settings.request.output = an::PresentationOutput::Interleaved8;
    an::Result<an::ui::ViewPresentationOutcome> presented =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, nullptr, &session.memory());
    if (!presented.hasValue())
    {
        return presented.status();
    }
    return presented->image;
}

/// Counts one piece of the comparison's work from its start until its completion has run or been dropped.
class PendingComparisonWork
{
public:
    explicit PendingComparisonWork(std::shared_ptr<std::atomic<std::uint32_t>> counter) : m_counter(std::move(counter))
    {
        m_counter->fetch_add(1u, std::memory_order_acq_rel);
    }

    PendingComparisonWork(const PendingComparisonWork&) = delete;
    PendingComparisonWork& operator=(const PendingComparisonWork&) = delete;

    ~PendingComparisonWork()
    {
        m_counter->fetch_sub(1u, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic<std::uint32_t>> m_counter;
};

/// Runs a step on a worker and its completion on the GUI thread while the widget lives.
template <typename WorkT, typename DoneT>
void runForComparison(an::ui::ComparisonViewWidget* widget, std::shared_ptr<std::atomic<std::uint32_t>> counter,
                      WorkT work, DoneT done)
{
    auto pending = std::make_shared<PendingComparisonWork>(std::move(counter));
    QPointer<an::ui::ComparisonViewWidget> guard(widget);
    QThreadPool::globalInstance()->start([guard, pending, work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<decltype(work())>(work());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, pending, result, done = std::move(done)]() mutable {
                if (guard)
                {
                    done(*guard, std::move(*result));
                }
            },
            Qt::QueuedConnection);
    });
}

} // namespace

namespace an::ui
{

class ComparisonViewWidget::ControlListener : public ImageViewControlListener
{
public:
    ControlListener(ComparisonViewWidget* owner, ImageViewControl* control)
        : ImageViewControlListener(control), m_owner(owner)
    {
    }

    void onImageLoaded(ImageViewControl* ctrl) override
    {
        if (m_owner)
        {
            m_owner->onControlImageLoaded(ctrl);
        }
    }

    void onImageViewScaled(ImageViewControl* ctrl) override
    {
        if (m_owner)
        {
            m_owner->onControlScaled(ctrl);
        }
    }

private:
    ComparisonViewWidget* m_owner = nullptr;
};

W_OBJECT_IMPL(an::ui::ComparisonViewWidget)

ImageViewControl* ComparisonViewWidget::createPreviewControl(QWidget* parent, const char* objectName)
{
    if (!an::ui::appContext())
    {
        return nullptr;
    }

    const QString controlName = QString::fromUtf8(objectName);
    auto* control = new ImageViewControl(parent);
    control->setObjectName(controlName);
    control->initialize(controlName.toStdString(), ViewControlMode::st_preview);
    control->setAutoDebayerCfaImagePreviews(true);
    control->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    return control;
}

ComparisonViewWidget::ComparisonViewWidget(QWidget* parent)
    : QWidget(parent), m_Differences(std::make_unique<RequestTracker>()), m_Overlays(std::make_unique<RequestTracker>())
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("comparisonSplitter"));
    layout->addWidget(m_splitter);

    m_leftControl = createPreviewControl(this, "comparisonLeftControl");
    m_rightControl = createPreviewControl(this, "comparisonRightControl");

    if (m_leftControl)
    {
        m_splitter->addWidget(m_leftControl);
    }
    if (m_rightControl)
    {
        m_splitter->addWidget(m_rightControl);
        m_rightControl->hide();
    }

    if (m_splitter->count() == 2)
    {
        m_splitter->setStretchFactor(0, 1);
        m_splitter->setStretchFactor(1, 1);
        m_splitter->setSizes({1, 1});
    }

    m_leftListener = std::make_unique<ControlListener>(this, m_leftControl);
    m_rightListener = std::make_unique<ControlListener>(this, m_rightControl);
    m_overlayModule = std::make_unique<ComparisonDisplayModule>();
    m_overlayModule->setOverlayOpacity(m_overlayOpacity);
    if (m_leftControl && m_leftControl->getViewWidget())
    {
        m_overlayModule->attachToViewWidget(m_leftControl->getViewWidget());
    }

    const auto connectPanSync = [this](ImageViewControl* source, ImageViewControl* target) {
        if (!source || !target || !source->getScrollArea())
        {
            return;
        }

        auto* horizontal = source->getScrollArea()->horizontalScrollBar();
        auto* vertical = source->getScrollArea()->verticalScrollBar();
        if (horizontal)
        {
            connect(horizontal, &QScrollBar::valueChanged, this, [this, source]() { syncPanFrom(source); });
        }
        if (vertical)
        {
            connect(vertical, &QScrollBar::valueChanged, this, [this, source]() { syncPanFrom(source); });
        }
    };

    connectPanSync(m_leftControl, m_rightControl);
    connectPanSync(m_rightControl, m_leftControl);
}

ComparisonViewWidget::~ComparisonViewWidget()
{
    // Work still on its way finds the widget gone through its guard; its tickets are refused from here.
    cancelPendingDifference();
    m_Overlays->cancel();
}

bool ComparisonViewWidget::isComparisonIdle() const
{
    return m_Outstanding->load(std::memory_order_acquire) == 0u;
}

void ComparisonViewWidget::showImage(ImageViewControl* control, an::consumer::ImageAsset&& image, const char* name)
{
    if (!control)
    {
        return;
    }
    if (!image.valid())
    {
        control->clear();
        refreshDisplayedState();
        return;
    }
    an::Result<an::ui::ImageDocument> document =
        an::ui::ImageDocument::create(std::move(image), an::ui::DocumentNames{.displayName = QString::fromUtf8(name)});
    if (!document.hasValue())
    {
        control->clear();
        refreshDisplayedState();
        return;
    }
    control->setDocument(std::move(*document));
}

void ComparisonViewWidget::setLeftImage(an::consumer::ImageAsset&& image)
{
    m_leftPath.clear();
    showImage(m_leftControl, std::move(image), "comparisonLeft");
}

void ComparisonViewWidget::setRightImage(an::consumer::ImageAsset&& image)
{
    m_rightPath.clear();
    showImage(m_rightControl, std::move(image), "comparisonRight");
}

void ComparisonViewWidget::loadLeftFromFile(const std::string& path)
{
    m_leftPath = QString::fromStdString(path);

    if (!m_leftControl)
    {
        return;
    }

    if (path.empty())
    {
        m_leftControl->clear();
        refreshDisplayedState();
        return;
    }

    m_leftControl->loadImageFromFile(path);
}

void ComparisonViewWidget::loadRightFromFile(const std::string& path)
{
    m_rightPath = QString::fromStdString(path);

    if (!m_rightControl)
    {
        return;
    }

    if (path.empty())
    {
        m_rightControl->clear();
        refreshDisplayedState();
        return;
    }

    m_rightControl->loadImageFromFile(path);
}

void ComparisonViewWidget::setMode(const ComparisonViewMode mode)
{
    if (m_mode == mode)
    {
        return;
    }

    m_mode = mode;

    if (m_rightControl)
    {
        m_rightControl->setVisible(m_mode == ComparisonViewMode::SideBySide);
    }

    refreshDisplayedState();
}

ComparisonViewMode ComparisonViewWidget::mode() const
{
    return m_mode;
}

void ComparisonViewWidget::setOverlayOpacity(const double opacity)
{
    m_overlayOpacity = opacity;
    if (m_overlayModule)
    {
        m_overlayModule->setOverlayOpacity(opacity);
    }
}

double ComparisonViewWidget::overlayOpacity() const
{
    return m_overlayOpacity;
}

void ComparisonViewWidget::fitToWindow()
{
    QScopedValueRollback<bool> zoomGuard(m_syncingZoom, true);

    if (m_leftControl)
    {
        m_leftControl->fitToWindow();
        m_leftControl->setViewportOffset(QPoint());
    }
    if (m_rightControl)
    {
        m_rightControl->fitToWindow();
        m_rightControl->setViewportOffset(QPoint());
    }

    if (m_zoomLocked)
    {
        captureLockedTransform(m_leftControl);
    }

    emitCurrentZoom();
}

void ComparisonViewWidget::setZoom(const double factor)
{
    if (m_leftControl && m_leftControl->getViewWidget())
    {
        m_leftControl->getViewWidget()->scaleImageView(factor, true);
    }
    if (m_zoomLocked && m_rightControl && m_rightControl->getViewWidget())
    {
        m_rightControl->getViewWidget()->scaleImageView(zoomFactorForTarget(m_leftControl, m_rightControl, factor),
                                                        true);
    }
    if (m_zoomLocked)
    {
        m_lockedZoomFactor = factor;
    }
}

void ComparisonViewWidget::zoomOneToOne()
{
    if (m_leftControl && m_leftControl->getViewWidget())
    {
        m_leftControl->getViewWidget()->scaleImageView(1.0, true);
        m_leftControl->setViewportOffset(QPoint());
    }
    if (m_rightControl && m_rightControl->getViewWidget())
    {
        m_rightControl->getViewWidget()->scaleImageView(zoomFactorForTarget(m_leftControl, m_rightControl, 1.0), true);
        m_rightControl->setViewportOffset(QPoint());
    }

    if (m_zoomLocked)
    {
        m_lockedZoomFactor = 1.0;
        m_lockedViewportOffset = QPoint();
    }
}

void ComparisonViewWidget::setZoomLocked(const bool locked)
{
    m_zoomLocked = locked;
    if (m_zoomLocked)
    {
        captureLockedTransform(m_leftControl);
    }
}

bool ComparisonViewWidget::isZoomLocked() const
{
    return m_zoomLocked;
}

ImageViewControl* ComparisonViewWidget::leftControl() const
{
    return m_leftControl;
}

ImageViewControl* ComparisonViewWidget::rightControl() const
{
    return m_rightControl;
}

void ComparisonViewWidget::onControlImageLoaded(ImageViewControl* source)
{
    if ((source == m_leftControl && m_leftInitialFitPending) || (source == m_rightControl && m_rightInitialFitPending))
    {
        applyInitialFitIfPending(source);
    }
    else if (m_zoomLocked && m_hasLockedTransform)
    {
        reapplyLockedTransformAsync();
    }

    if (m_mode == ComparisonViewMode::Overlay)
    {
        updateOverlayState();
    }
    else if (m_mode == ComparisonViewMode::Difference)
    {
        requestDifferenceImage();
    }
}

void ComparisonViewWidget::onControlScaled(ImageViewControl* source)
{
    if (!source)
    {
        return;
    }

    // Do not capture while a pending restore is queued: the auto-fit that fires after
    // each image load would overwrite m_lockedZoomFactor before applyLockedTransform
    // gets a chance to restore the user's zoom.
    if (m_zoomLocked && !m_restoreQueued)
    {
        captureLockedTransform(source);
    }

    if (m_syncingZoom)
    {
        emitCurrentZoom();
        return;
    }

    ImageViewControl* target = (source == m_leftControl) ? m_rightControl : m_leftControl;
    if (!target || !target->getViewWidget())
    {
        emitCurrentZoom();
        return;
    }

    QScopedValueRollback<bool> guard(m_syncingZoom, true);
    target->getViewWidget()->scaleImageView(
        zoomFactorForTarget(source, target, static_cast<double>(source->getCurrentZoom()) / 100.0), true);
    emitCurrentZoom();
}

void ComparisonViewWidget::syncPanFrom(ImageViewControl* source)
{
    if (!source || m_syncingPan)
    {
        return;
    }

    ImageViewControl* target = (source == m_leftControl) ? m_rightControl : m_leftControl;
    if (!target)
    {
        return;
    }

    if (m_zoomLocked && !m_restoreQueued)
    {
        captureLockedTransform(source);
    }

    QScopedValueRollback<bool> guard(m_syncingPan, true);
    target->setViewportOffset(viewportOffsetForTarget(source, target, source->getViewportOffset(),
                                                      static_cast<double>(source->getCurrentZoom()) / 100.0,
                                                      static_cast<double>(target->getCurrentZoom()) / 100.0));
}

void ComparisonViewWidget::applyInitialFitIfPending(ImageViewControl* source)
{
    if (!source || !source->hasImage())
    {
        return;
    }

    bool* pending = nullptr;
    if (source == m_leftControl)
    {
        pending = &m_leftInitialFitPending;
    }
    else if (source == m_rightControl)
    {
        pending = &m_rightInitialFitPending;
    }

    if (!pending || !*pending)
    {
        return;
    }

    *pending = false;

    {
        QScopedValueRollback<bool> zoomGuard(m_syncingZoom, true);
        source->fitToWindow();
        source->setViewportOffset(QPoint());
    }

    if (m_zoomLocked && source == m_leftControl)
    {
        captureLockedTransform(source);
    }

    emitCurrentZoom();
}

void ComparisonViewWidget::captureLockedTransform(ImageViewControl* source)
{
    if (!m_zoomLocked || !source || !source->hasImage())
    {
        return;
    }

    const double sourceFactor = static_cast<double>(source->getCurrentZoom()) / 100.0;
    m_lockedZoomFactor = zoomFactorForTarget(source, m_leftControl, sourceFactor);
    m_lockedViewportOffset =
        viewportOffsetForTarget(source, m_leftControl, source->getViewportOffset(), sourceFactor, m_lockedZoomFactor);
    m_hasLockedTransform = true;
}

double ComparisonViewWidget::zoomFactorForTarget(ImageViewControl* source, ImageViewControl* target,
                                                 const double sourceFactor) const
{
    if (!source || !target || source == target || !isUsablePositive(sourceFactor))
    {
        return sourceFactor;
    }

    const ImagePixelSize sourceSize = imagePixelSize(source);
    const ImagePixelSize targetSize = imagePixelSize(target);
    if (!sourceSize.isValid() || !targetSize.isValid())
    {
        return sourceFactor;
    }

    const double sourceArea = sourceSize.width * sourceSize.height;
    const double targetArea = targetSize.width * targetSize.height;
    if (!isUsablePositive(sourceArea) || !isUsablePositive(targetArea))
    {
        return sourceFactor;
    }

    const double ratio = std::sqrt(sourceArea / targetArea);
    return isUsablePositive(ratio) ? sourceFactor * ratio : sourceFactor;
}

QPoint ComparisonViewWidget::viewportOffsetForTarget(ImageViewControl* source, ImageViewControl* target,
                                                     const QPoint& sourceOffset, const double sourceFactor,
                                                     const double targetFactor) const
{
    if (!source || !target || source == target || !isUsablePositive(sourceFactor) || !isUsablePositive(targetFactor))
    {
        return sourceOffset;
    }

    const ImagePixelSize sourceSize = imagePixelSize(source);
    const ImagePixelSize targetSize = imagePixelSize(target);
    if (!sourceSize.isValid() || !targetSize.isValid())
    {
        return sourceOffset;
    }

    const double sourceDisplayWidth = sourceSize.width * sourceFactor;
    const double sourceDisplayHeight = sourceSize.height * sourceFactor;
    const double targetDisplayWidth = targetSize.width * targetFactor;
    const double targetDisplayHeight = targetSize.height * targetFactor;
    if (!isUsablePositive(sourceDisplayWidth) || !isUsablePositive(sourceDisplayHeight) ||
        !isUsablePositive(targetDisplayWidth) || !isUsablePositive(targetDisplayHeight))
    {
        return sourceOffset;
    }

    return QPoint(static_cast<int>(std::lround(sourceOffset.x() * targetDisplayWidth / sourceDisplayWidth)),
                  static_cast<int>(std::lround(sourceOffset.y() * targetDisplayHeight / sourceDisplayHeight)));
}

void ComparisonViewWidget::reapplyLockedTransformAsync()
{
    if (!m_zoomLocked || m_restoreQueued)
    {
        return;
    }

    m_restoreQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            applyLockedTransform();
            m_restoreQueued = false;
        },
        Qt::QueuedConnection);
}

void ComparisonViewWidget::applyLockedTransform()
{
    if (!m_zoomLocked)
    {
        return;
    }

    {
        QScopedValueRollback<bool> zoomGuard(m_syncingZoom, true);
        const double leftZoomFactor = m_lockedZoomFactor;
        const double rightZoomFactor = zoomFactorForTarget(m_leftControl, m_rightControl, leftZoomFactor);
        if (m_leftControl && m_leftControl->getViewWidget())
        {
            m_leftControl->getViewWidget()->scaleImageView(leftZoomFactor, true);
        }
        if (m_rightControl && m_rightControl->getViewWidget())
        {
            m_rightControl->getViewWidget()->scaleImageView(rightZoomFactor, true);
        }
    }

    {
        QScopedValueRollback<bool> panGuard(m_syncingPan, true);
        const double leftZoomFactor = m_lockedZoomFactor;
        const double rightZoomFactor = zoomFactorForTarget(m_leftControl, m_rightControl, leftZoomFactor);
        if (m_leftControl)
        {
            m_leftControl->setViewportOffset(m_lockedViewportOffset);
        }
        if (m_rightControl)
        {
            m_rightControl->setViewportOffset(viewportOffsetForTarget(
                m_leftControl, m_rightControl, m_lockedViewportOffset, leftZoomFactor, rightZoomFactor));
        }
    }

    emitCurrentZoom();
}

void ComparisonViewWidget::emitCurrentZoom()
{
    if (!m_leftControl)
    {
        return;
    }

    zoomChanged(m_leftControl->getCurrentZoom() / 100.0);
}

void ComparisonViewWidget::refreshDisplayedState()
{
    if (!m_rightControl)
    {
        return;
    }

    m_rightControl->setVisible(m_mode == ComparisonViewMode::SideBySide);

    switch (m_mode)
    {
    case ComparisonViewMode::Single:
    case ComparisonViewMode::SideBySide:
        cancelPendingDifference();
        if (m_overlayModule)
        {
            m_overlayModule->clearOverlayImage();
        }
        restoreBaseImages();
        break;
    case ComparisonViewMode::Overlay:
        cancelPendingDifference();
        restoreBaseImages();
        updateOverlayState();
        break;
    case ComparisonViewMode::Difference:
        if (m_overlayModule)
        {
            m_overlayModule->clearOverlayImage();
        }
        requestDifferenceImage();
        break;
    }
}

void ComparisonViewWidget::restoreBaseImages()
{
    // The difference is shown as the left view's alternate image, so its own image was never replaced: showing it
    // again is turning the alternate off.
    if (m_leftControl && m_leftControl->getViewWidget() &&
        m_leftControl->getViewWidget()->isAlternateImageRenderEnabled())
    {
        m_leftControl->getViewWidget()->enableAlternateImageRendering(false);
    }
    if (m_leftControl)
    {
        m_leftControl->clearAlternateImage();
    }
}

void ComparisonViewWidget::updateOverlayState()
{
    if (!m_overlayModule)
    {
        return;
    }

    m_overlayModule->setOverlayOpacity(m_overlayOpacity);
    const an::consumer::EngineSession* session = comparisonSession();
    if (!m_rightControl || !m_rightControl->hasImage() || session == nullptr)
    {
        m_Overlays->cancel();
        m_overlayModule->clearOverlayImage();
        return;
    }

    an::Result<CommitTicket> ticket = m_Overlays->issue(m_rightControl->document());
    if (!ticket.hasValue())
    {
        m_overlayModule->clearOverlayImage();
        return;
    }
    auto issued = std::make_shared<const CommitTicket>(std::move(*ticket));
    const std::shared_ptr<const an::consumer::ImageAsset> right = m_rightControl->document().asset();
    runForComparison(
        this, m_Outstanding,
        [session, right, stop = issued->stopToken()]() { return presentOverlay(*session, *right, stop); },
        [issued](ComparisonViewWidget& widget, an::Result<std::shared_ptr<const PresentedImage>>&& presented) {
            if (issued->verdict() != CommitVerdict::Commit || widget.m_mode != ComparisonViewMode::Overlay ||
                !widget.m_overlayModule)
            {
                return;
            }
            if (!presented.hasValue())
            {
                widget.m_overlayModule->clearOverlayImage();
                return;
            }
            widget.m_overlayModule->setOverlayImage(presentedQImage(*presented));
        });
}

void ComparisonViewWidget::requestDifferenceImage()
{
    cancelPendingDifference();

    if (m_mode != ComparisonViewMode::Difference)
    {
        return;
    }

    const an::consumer::EngineSession* session = comparisonSession();
    if (!m_leftControl || !m_rightControl || !m_leftControl->hasImage() || !m_rightControl->hasImage() ||
        session == nullptr)
    {
        return;
    }

    const ImageDocument* secondary[] = {&m_rightControl->document()};
    an::Result<CommitTicket> ticket = m_Differences->issue(m_leftControl->document(), secondary);
    if (!ticket.hasValue())
    {
        return;
    }
    auto issued = std::make_shared<const CommitTicket>(std::move(*ticket));
    const std::shared_ptr<const an::consumer::ImageAsset> left = m_leftControl->document().asset();
    const std::shared_ptr<const an::consumer::ImageAsset> right = m_rightControl->document().asset();
    runForComparison(
        this, m_Outstanding,
        [session, left, right, stop = issued->stopToken()]() {
            return buildDifferenceImage(*session, *left, *right, {}, stop);
        },
        [issued](ComparisonViewWidget& widget, an::Result<an::consumer::ImageAsset>&& difference) {
            // A difference of images either view no longer shows, or of a request superseded, is never shown.
            if (issued->verdict() != CommitVerdict::Commit || widget.m_mode != ComparisonViewMode::Difference ||
                !widget.m_leftControl)
            {
                return;
            }
            if (!difference.hasValue())
            {
                LOG_WARNING("Comparison: the difference could not be built: {}",
                            std::string(difference.status().message()));
                return;
            }
            widget.m_leftControl->setAlternateAsset(
                std::make_shared<const an::consumer::ImageAsset>(std::move(*difference)), false);
            if (widget.m_leftControl->getViewWidget())
            {
                widget.m_leftControl->getViewWidget()->enableAlternateImageRendering(true);
            }
            if (widget.m_zoomLocked)
            {
                widget.reapplyLockedTransformAsync();
            }
        });
}

void ComparisonViewWidget::cancelPendingDifference()
{
    m_Differences->cancel();
}

} // namespace an::ui
