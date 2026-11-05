// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QPoint>
#include <QString>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

class ImageViewControl;
class ImageViewControlListener;
class QSplitter;

namespace an::ui
{

class ComparisonDisplayModule;
class RequestTracker;

enum class ComparisonViewMode
{
    Single,
    SideBySide,
    Overlay,
    Difference
};

class AFTERNIGHTCONTROLS_API ComparisonViewWidget : public QWidget
{
    W_OBJECT(ComparisonViewWidget)

public:
    explicit ComparisonViewWidget(QWidget* parent = nullptr);
    ~ComparisonViewWidget() override;

    /// Shows an image on the left: the left view takes it as a new document.
    void setLeftImage(an::consumer::ImageAsset&& image);

    /// Shows an image on the right: the right view takes it as a new document.
    void setRightImage(an::consumer::ImageAsset&& image);
    void loadLeftFromFile(const std::string& path);
    void loadRightFromFile(const std::string& path);

    void setMode(ComparisonViewMode mode);
    [[nodiscard]] ComparisonViewMode mode() const;

    void setOverlayOpacity(double opacity);
    [[nodiscard]] double overlayOpacity() const;

    void fitToWindow();
    void zoomOneToOne();
    void setZoom(double factor);
    void setZoomLocked(bool locked);
    [[nodiscard]] bool isZoomLocked() const;

    [[nodiscard]] ImageViewControl* leftControl() const;
    [[nodiscard]] ImageViewControl* rightControl() const;

    /// Whether no difference or overlay of the comparison is on its way.
    [[nodiscard]] bool isComparisonIdle() const;

    void zoomChanged(double zoomFactor) W_SIGNAL(zoomChanged, zoomFactor);

private:
    class ControlListener;

    static ImageViewControl* createPreviewControl(QWidget* parent, const char* objectName);

    void onControlImageLoaded(ImageViewControl* source);
    void onControlScaled(ImageViewControl* source);
    void syncPanFrom(ImageViewControl* source);

    void applyInitialFitIfPending(ImageViewControl* source);
    void captureLockedTransform(ImageViewControl* source);
    [[nodiscard]] double zoomFactorForTarget(ImageViewControl* source, ImageViewControl* target,
                                             double sourceFactor) const;
    [[nodiscard]] QPoint viewportOffsetForTarget(ImageViewControl* source, ImageViewControl* target,
                                                 const QPoint& sourceOffset, double sourceFactor,
                                                 double targetFactor) const;
    void reapplyLockedTransformAsync();
    void applyLockedTransform();
    void emitCurrentZoom();

    void refreshDisplayedState();
    void restoreBaseImages();
    void updateOverlayState();
    void requestDifferenceImage();
    void cancelPendingDifference();
    void showImage(ImageViewControl* control, an::consumer::ImageAsset&& image, const char* name);

    QSplitter* m_splitter = nullptr;
    ImageViewControl* m_leftControl = nullptr;
    ImageViewControl* m_rightControl = nullptr;
    std::unique_ptr<ControlListener> m_leftListener;
    std::unique_ptr<ControlListener> m_rightListener;
    std::unique_ptr<ComparisonDisplayModule> m_overlayModule;

    QString m_leftPath;
    QString m_rightPath;

    ComparisonViewMode m_mode = ComparisonViewMode::Single;
    double m_overlayOpacity = 0.5;
    bool m_zoomLocked = true;
    bool m_syncingZoom = false;
    bool m_syncingPan = false;
    bool m_restoreQueued = false;
    bool m_hasLockedTransform = false;
    bool m_leftInitialFitPending = true;
    bool m_rightInitialFitPending = true;
    double m_lockedZoomFactor = 1.0;
    QPoint m_lockedViewportOffset;
    std::unique_ptr<RequestTracker> m_Differences;
    std::unique_ptr<RequestTracker> m_Overlays;
    std::shared_ptr<std::atomic<std::uint32_t>> m_Outstanding = std::make_shared<std::atomic<std::uint32_t>>(0u);
};

} // namespace an::ui
