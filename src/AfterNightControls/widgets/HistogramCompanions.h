// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The histograms other parts of Controls show without owning: the lens's, made by `createLensHistogram`, a process
/// dialog's and the history window's, made by `createPreviewHistogram`, and the scheduler every histogram over an image
/// counts through.
///
/// The view and the process framework declare the interfaces in `controls/ImageViewCompanions.h` and
/// `controls/ProcessCompanions.h` and include nothing of the histogram widgets; their definitions live here, beside the
/// widgets. A histogram over an image is counted on the thread pool, in an operation the process's session admits, and
/// shown on the GUI thread only when it is still the latest one asked for and its widget still exists. A count someone
/// asked for is admitted at the session's width; only a real-time preview's histogram, counted again with every
/// preview, is admitted as a preview and shares the threads kept for previews.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ProcessCompanions.h"
#include "widgets/HistogramDisplayData.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>

#include <QPointer>

#include <cstdint>
#include <memory>
#include <stop_token>

class HistogramWidget;
class QScrollArea;
class QWidget;

namespace an::ui
{

/// Counts an image read under the session for a histogram widget.
///
/// @param session the session the counting is admitted by.
/// @param image the image, read under its own lease.
/// @param resolution how many bins.
/// @param stop stops the counting while it waits for admission, for the image, or runs.
/// @param purpose what the counting is admitted as: by default a count someone asked for, at the session's width;
/// `Preview` for one that is part of a preview, within the threads kept for previews.
/// @return the counts; or why they were not counted.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<HistogramDisplayData>
countHistogram(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
               HistogramResolution resolution, std::stop_token stop = {},
               an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply);

/// What a scheduler's counts came to, for the cases that hold it to its rules.
struct HistogramCountTelemetry
{
    /// Counts started on the thread pool.
    std::uint64_t started = 0u;

    /// Counts shown in the widget.
    std::uint64_t shown = 0u;

    /// Counts that landed after a later one was asked for, and were dropped.
    std::uint64_t superseded = 0u;

    /// Counts that failed, or found no session to count in; the widget then shows nothing.
    std::uint64_t failed = 0u;

    /// Counts made elsewhere and handed in to be shown as they came, with nothing counted here.
    std::uint64_t handed = 0u;
};

/// Keeps one histogram widget showing the counts of the image it was last given.
///
/// Every request supersedes and stops the one before it, and only the latest one asked for is shown; one that lands
/// after its widget or its scheduler is gone is dropped. Called on the GUI thread only.
class AFTERNIGHTCONTROLS_API HistogramCountScheduler
{
public:
    /// A scheduler for a widget, which it neither owns nor outlives the use of.
    ///
    /// @param widget the widget the counts are shown in.
    /// @param purpose what its counts are admitted as, as `countHistogram` takes it.
    explicit HistogramCountScheduler(HistogramWidget* widget,
                                     an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply);

    /// Stops the count on its way; it is dropped when it lands.
    ~HistogramCountScheduler();

    HistogramCountScheduler(const HistogramCountScheduler&) = delete;
    HistogramCountScheduler& operator=(const HistogramCountScheduler&) = delete;

    /// Counts an image at the widget's resolution and shows it when it lands.
    ///
    /// @param image the image, held while it is counted; null clears the widget at once.
    void count(std::shared_ptr<const an::consumer::ImageAsset> image);

    /// Counts the image last given again, at the widget's resolution as it is now.
    void recount();

    /// Shows counts made elsewhere at once, in place of a count: one on its way is stopped and never shown, and the
    /// image last given is forgotten, so a later `recount` clears the widget.
    ///
    /// @param counts the counts; null clears the widget.
    void show(std::shared_ptr<const HistogramDisplayData> counts);

    /// The image last given.
    [[nodiscard]] const std::shared_ptr<const an::consumer::ImageAsset>& image() const noexcept;

    /// What the counts came to so far.
    [[nodiscard]] HistogramCountTelemetry telemetry() const;

    /// Whether every count asked for has landed.
    [[nodiscard]] bool isIdle() const;

private:
    struct State;

    QPointer<HistogramWidget> m_widget;
    an::consumer::OperationPurpose m_purpose;
    std::shared_ptr<State> m_state;
    std::shared_ptr<const an::consumer::ImageAsset> m_image;
    std::stop_source m_stop;
};

/// The histogram resolution a process dialog's names.
///
/// @param resolution the dialog's resolution.
/// @return the same number of bins.
[[nodiscard]] AFTERNIGHTCONTROLS_API HistogramResolution
histogramResolutionOf(PreviewHistogramResolution resolution) noexcept;

/// The lens's histogram: a small histogram widget drawing the counts the view hands it.
class AFTERNIGHTCONTROLS_API LensHistogramView final : public ILensHistogram
{
public:
    /// Makes the widget as a child of the lens, with the lens's drawing settings.
    ///
    /// @param parent the lens.
    explicit LensHistogramView(QWidget* parent);

    /// The widget is the lens's child and goes with it.
    ~LensHistogramView() override;

    /// The widget the lens lays out; null once the lens is gone.
    [[nodiscard]] QWidget* widget() override;

    /// Shows a region's counts.
    ///
    /// @param counts the counts, one to three channels of `kLensHistogramBins` bins.
    void setCounts(const LensHistogram& counts) override;

private:
    QPointer<HistogramWidget> m_widget;
};

/// A process dialog's histogram: a widget inside the dialog's scroll area showing the image it was last given, counted
/// through a scheduler.
class AFTERNIGHTCONTROLS_API PreviewHistogramView final : public IPreviewHistogram
{
public:
    /// Makes the widget and sets it as the scroll area's.
    ///
    /// @param scrollArea the scroll area that shows it.
    /// @param purpose what its counts are admitted as, as `createPreviewHistogram` takes it.
    explicit PreviewHistogramView(QScrollArea* scrollArea,
                                  an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply);

    /// Stops the count on its way; the widget is the scroll area's and goes with it.
    ~PreviewHistogramView() override;

    [[nodiscard]] QWidget* widget() override;
    void setImage(std::shared_ptr<const an::consumer::ImageAsset> image) override;
    void showCounts(std::shared_ptr<const HistogramDisplayData> counts) override;
    [[nodiscard]] PreviewHistogramResolution resolution() const override;
    void setDisplayedChannels(bool red, bool green, bool blue) override;
    void setResolution(PreviewHistogramResolution resolution) override;
    void setGridEnabled(bool enabled) override;
    void zoomIn() override;
    void zoomOut() override;
    void resetZoom() override;
    void setLocalChannelNormalization(bool enabled) override;
    void setClippingInfoEnabled(bool enabled) override;
    void setAreaModeEnabled(bool enabled) override;

    /// The scheduler the image is counted through, for the cases that hold it to its rules.
    [[nodiscard]] const HistogramCountScheduler& scheduler() const noexcept;

private:
    QPointer<HistogramWidget> m_widget;
    std::unique_ptr<HistogramCountScheduler> m_scheduler;
    PreviewHistogramResolution m_resolution = PreviewHistogramResolution::Bits8;
};

} // namespace an::ui
