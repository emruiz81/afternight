// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The window over one view's history: its timeline with the inherited lineage above it, a preview and histogram of
/// the step selected, and the actions that jump to, clone and remove a step and save the history as text.
///
/// Selecting a step previews it without changing the view: the step's image is read and presented on a worker and
/// shown when it lands, if the selection has not moved on. Cloning reads the step the same way and makes a new view of
/// it; jumping and removing ask the history, which restores on a worker of its own.
#pragma once

#include "AfterNightControlsExport.h"
#include "ImageHistoryControlUiBuilder.h"
#include "ImageHistoryTracker.h"
#include "controls/ImageViewControlListener.h"

#include <QDialog>
#include <QPoint>

#include <cstdint>
#include <memory>
#include <stop_token>

class ImageViewControl;
class ImagePreviewWidget;

namespace an::ui
{
class IPreviewHistogram;
class PresentedImage;
} // namespace an::ui

class AFTERNIGHTCONTROLS_API ImageHistoryControl : public QDialog,
                                                   public ImageViewControlListener,
                                                   public ImageHistoryTrackerListener
{
public:
    /// A window over `tracker`, the history of `control`, parented to the view. It holds the tracker weakly: the
    /// history closes its windows before it goes.
    ImageHistoryControl(ImageViewControl* control, const std::shared_ptr<ImageHistoryTracker>& tracker);

    void onImageRenamed(ImageViewControl* ctrl) override;

    void onImageHistoryUpdated() override;

    void recalculate();

    /// How many step previews have been shown since the window opened; a test reads it.
    [[nodiscard]] std::uint64_t previewsShown() const;

    /// The bytes the preview shows now, or null when it shows none.
    [[nodiscard]] std::shared_ptr<const an::ui::PresentedImage> shownPreview() const;

    ~ImageHistoryControl() override;

private:
    void on_refreshButtonPushed();
    void on_closeButtonPushed();
    void on_moveUpButtonPushed();
    void on_moveDownButtonPushed();
    void on_toggleLineagePushed();
    void on_historyTableSelectionChanged();
    void on_historyTableDoubleClicked(int row, int column);
    void on_historyContextMenuRequested(const QPoint& pos);

    void initialize();

    void fillTableItem(int row, const ImageHistoryStepSPtr& step, bool isCurrentStep);

    void refillData();
    void updateNavigationButtons();
    void updateSelectedStepPreview();
    void jumpToSelectedStep();
    void cloneSelectedStep();
    void removeSelectedStep();
    void saveHistoryLog();
    [[nodiscard]] int getSelectedStepRow() const;
    [[nodiscard]] ImageHistoryStepSPtr getStepForRow(int row) const;
    void clearSelectedStepPreview();
    [[nodiscard]] std::shared_ptr<ImageHistoryTracker> tracker() const;

    an::ui::controls::ImageHistoryControlUiElements m_ui;
    std::weak_ptr<ImageHistoryTracker> m_Tracker;
    ImagePreviewWidget* m_stepPreviewWidget = nullptr;
    std::unique_ptr<an::ui::IPreviewHistogram> m_stepHistogram;
    int m_LineageRowCount = 0;
    bool m_LineageVisible = true;

    // The preview on its way: a later selection stops it, and its result is shown only while it is the newest.
    std::stop_source m_PreviewStop;
    std::uint64_t m_PreviewRequest = 0u;
    std::uint64_t m_PreviewsShown = 0u;
    std::shared_ptr<const an::ui::PresentedImage> m_ShownPreview;
};
