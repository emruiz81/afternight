// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Compare: two open views of one size shown through the preview, toggled with the preview's switch or split
/// across it, each with its own histogram. The second image is shown as the preview shows the first - presented by the
/// engine under the preview's own stretch, on a worker - and nothing of the images is computed by the window itself.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/ImageCompareProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <memory>
#include <string>
#include <variant>

class HistogramWidget;
class ImageViewControl;
class QAbstractButton;
class QLabel;
class QWidget;

namespace an::ui
{
class HistogramCountScheduler;
class PresentedImage;
class SplitComparisonDisplayModule;
} // namespace an::ui

/// Image Compare: the target and a second view of its size, compared through the preview. Toggle shows one or the other
/// with the preview's switch; a horizontal or vertical split shows the second past a splitter dragged across the
/// preview, swapped with a right click and centred again with a double click. The second image is presented for the
/// split on a worker, through the engine, as the preview's own stretch presents the first; the lens over the second's
/// side magnifies that presentation. Each image has a histogram of its own under the preview's histogram buttons. The
/// window changes nothing: its run copies the target.
class AFTERNIGHTPROCESSES_API ImageCompareProcess : public an::ui::TypedProcessControl<std::monostate>
{
public:
    explicit ImageCompareProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void refreshTheme() override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) override;
    void onAlternateImageEnabled(ImageViewControl* ctrl) override;
    void onAlternateImageDisabled(ImageViewControl* ctrl) override;

    /// How the two images are compared now.
    [[nodiscard]] an::ui::processes::ImageCompareMode compareMode() const noexcept;

    /// Sets how the two images are compared, as the mode buttons do.
    void setCompareMode(an::ui::processes::ImageCompareMode mode);

    /// Whether a presentation of the second image for the split is on its way.
    [[nodiscard]] bool isOverlayPending() const noexcept;

    /// How many presentations of the second image the split has shown.
    [[nodiscard]] std::uint64_t overlaysShown() const noexcept;

    /// What the split's presentation shares with the worker that makes it, which the worker holds while it runs: a
    /// case checks that the window's going frees nothing under a worker on its way.
    [[nodiscard]] std::weak_ptr<const void> overlayWorkHandle() const noexcept;

    /// Whether the lens magnifies the second image's presentation rather than the view's image: the cursor is on
    /// the second image's side of the split and the presentation has landed.
    [[nodiscard]] bool lensShowsTheSecondImage() const noexcept;

    /// The split module the preview draws through.
    [[nodiscard]] an::ui::SplitComparisonDisplayModule* splitModule() const noexcept;

    /// The histogram of the first image and of the second.
    [[nodiscard]] HistogramWidget* primaryHistogram() const noexcept;
    [[nodiscard]] HistogramWidget* secondaryHistogram() const noexcept;

    /// Whether both histograms' counts have landed.
    [[nodiscard]] bool histogramsIdle() const;

    ~ImageCompareProcess() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onDisplayModeChanged(ImageViewMode mode) override;

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const std::monostate& params,
                                       an::ui::ExecutionScope& scope) override;

    [[nodiscard]] std::monostate getParams() const override
    {
        return {};
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> getAlternateAsset() const override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_secondImageSelector_currentTextChanged(const QString& str);
    void on_compareModeChanged(int modeId);

    [[nodiscard]] bool isPreviewEnabled() const override;
    [[nodiscard]] bool requiresExclusivePreviewAccess() const override;
    void enablePreview(bool enabled) override;
    void on_targetImageSelector_currentTextChangedInternal(const QString& str) override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode) override;
    void previewControlStarted() override;
    void previewControlClosed() override;

    [[nodiscard]] bool isViewControlSupportedCustom(const ImageViewControl* ctrl) const;
    [[nodiscard]] ImageViewControl* secondView() const;
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> secondAsset() const;

    void setupCompareHistogramPanel();
    void setupCompareHistogramConnections();
    void refreshCompareHistogramLabelStyles();
    void updateCompareHistogramPreview();
    void updateCompareHistogramLabels();
    void updateCompareHistogramVisibility();
    void updateCompareHistogramDisplayChannels(int mode);
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> primaryHistogramAsset() const;
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> secondaryHistogramAsset() const;

    void cleanupPreviewBindings();
    /// Fills the second selector with the views of the target's size, leaving one out.
    void loadImage2List(const ImageViewControl* except = nullptr);
    void applyCompareMode();
    void updateSplitOverlayImage();
    void landOverlay(std::uint64_t generation, std::shared_ptr<const an::ui::PresentedImage> presented);
    void swapSplitImages();
    void updateQuickZoomOverrideForPos(const QPoint& widgetPos);
    void setSplitHoverTrackingEnabled(bool enabled);
    [[nodiscard]] QWidget* splitInteractionViewport() const;
    [[nodiscard]] bool isSplitInteractionTarget(QObject* watched) const;
    [[nodiscard]] QPoint mapSplitEventPosToWidget(QObject* watched, const QPoint& pos) const;
    void applySplitCursor() const;
    void restoreSplitCursor() const;

    an::ui::processes::ImageCompareProcessUiElements m_ui;
    std::unique_ptr<an::ui::SplitComparisonDisplayModule> m_splitModule;
    an::ui::processes::ImageCompareMode m_compareMode = an::ui::processes::ImageCompareMode::Toggle;

    QWidget* m_compareHistogramPanel = nullptr;
    QLabel* m_primaryHistogramLabel = nullptr;
    QLabel* m_secondaryHistogramLabel = nullptr;
    HistogramWidget* m_primaryHistogramWidget = nullptr;
    HistogramWidget* m_secondaryHistogramWidget = nullptr;
    std::unique_ptr<an::ui::HistogramCountScheduler> m_primaryHistogramCounts;
    std::unique_ptr<an::ui::HistogramCountScheduler> m_secondaryHistogramCounts;

    /// The second image as last presented for the split, which the lens magnifies over its side.
    std::shared_ptr<const an::ui::PresentedImage> m_overlayPresented;

    /// Whether the lens was last given that presentation.
    bool m_lensOnSecond = false;

    struct OverlayWork;
    std::shared_ptr<OverlayWork> m_overlay;

    bool m_IgnoreLastImage2SelectorTextChangedEvent = false;
    bool m_suppressAlternateCallbacks = false;
    bool m_splitHoverTrackingEnabled = false;
    bool m_previewMouseTrackingWasEnabled = false;
    bool m_previewViewportMouseTrackingWasEnabled = false;
};
