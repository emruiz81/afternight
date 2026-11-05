// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include "processes/HistogramTransformationProcessUiBuilder.h"
#include "processes/StretchProcessSupport.h"
#include "controls/TypedProcessControl.h"
#include "widgets/HistogramMTFWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>

#include <QRect>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class ImageViewControl;

/// The automatic stretch the dialog's buttons choose, or none for the dialog's own shadows, midtones and highlights.
enum class HistogramAutoStretch : std::uint8_t
{
    None,
    Linked,
    Unlinked
};

/// The shadows clipping, in sigmas, and the target background the automatic stretch derives its transfer from.
inline constexpr double kHistogramAutoStretchShadowsClipping = -2.8;
inline constexpr double kHistogramAutoStretchTargetBackground = 0.25;

struct HistogramTransformationParams
{
    double shadows = 0.0;
    double midtones = 0.5;
    double highlights = 1.0;

    /// What the transfer changes: the whole image, or one of its stored channels.
    an::ChannelScope channel = an::ChannelScope::Whole;

    /// The channel selector's text, for the history.
    std::string channelLabel = "RGB";

    HistogramAutoStretch autoStretch = HistogramAutoStretch::None;

    /// The image the dialog targeted when the run was issued, whose statistics stretch a preview area.
    std::shared_ptr<const an::consumer::ImageAsset> target;

    /// The preview area shown when the run was issued; empty for the whole image.
    QRect area;

    /// The dialog's measurements of its target, shared with its runs.
    std::shared_ptr<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>> statistics;
};

/// The screen transfer each channel takes and whether it changes, for parameters and the statistics of the whole image
/// an automatic stretch is derived from.
struct HistogramTransfer
{
    std::array<an::MidtonesTransferParameters, 3u> channels{};
    std::array<bool, 4u> changed{true, true, true, false};
};

/// The transfer the dialog's parameters ask for: their shadows, midtones and highlights on the channel they name, or
/// the automatic stretch derived from the statistics, linked or channel by channel, on every channel.
///
/// @param params the dialog's parameters.
/// @param statistics the whole image's statistics; read only by an automatic stretch.
/// @return the transfer; or what the derivation refused, `ErrorCode::NotInitialized` for an automatic stretch with no
///         statistics.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<HistogramTransfer>
histogramTransferOf(const HistogramTransformationParams& params,
                    const an::ui::processes::ScreenTransferStatistics* statistics);

class AFTERNIGHTPROCESSES_API HistogramTransformationProcess
    : public an::ui::TypedProcessControl<HistogramTransformationParams>,
      public HistogramMTFWidgetListener
{
public:
    HistogramTransformationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onAlternateImageEnabled(ImageViewControl* ctrl) override;
    void onAlternateImageDisabled(ImageViewControl* ctrl) override;
    void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) override;

    void onShadowsChanged(double shadows) override;
    void onMidtonesChanged(double midtones) override;
    void onHightlightsChanged(double highlights) override;
    void onSettingsUpdated() override;

    ~HistogramTransformationProcess() override;

private:
    void on_channelSelector2_currentTextChanged(const QString& str);
    void on_plotResolution_currentTextChanged(const QString& str);
    void refreshTheme() override;

    void on_shadowsValueChanged(double value);
    void on_midtonesValueChanged(double value);
    void on_highlightsValueChanged(double value);
    void on_PreviewClicked2(int state);
    void on_gridClicked(bool checked);
    void on_transferCurveClicked(bool checked);
    void on_ZoomInButton();
    void on_ZoomOutButton();
    void on_ZoomResetButton();
    void on_LocalChannelNormalization(bool checked);
    void on_showHistogramAreas(bool checked);

    void on_AutostretchLinkedButton();
    void on_AutostretchUnlinkedButton();

    void targetChanged();
    void updateSupportedImageChannels();
    void updateSupportedPlotResolutions();
    void clearAutoStretchState() noexcept;
    [[nodiscard]] bool isColourTarget() const;

    void resetSettings() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const HistogramTransformationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const HistogramTransformationParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const HistogramTransformationParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    /// Counts the target at the dialog's resolution on a worker, shows it as the original histogram and remaps it.
    void recountOriginal();

    /// Remaps the original counts through the transfer the dialog holds on a worker, and shows them as the preview
    /// histogram with the clipped samples.
    void recalculateHistogramPreview();

    void showClippedInfo(const an::ui::processes::ClippedSamples& clipped, std::uint64_t samples);

    [[nodiscard]] HistogramTransformationParams getParams() const override;

    std::unique_ptr<HistogramMTFWidget> m_original_histogram_widget;
    std::unique_ptr<HistogramMTFWidget> m_preview_histogram_widget;

    an::ui::processes::HistogramTransformationProcessUiElements m_ui;

    bool m_IgnoreLastValueChangeForRangeSelectors = false;

    HistogramAutoStretch m_AutoStretch = HistogramAutoStretch::None;

    std::shared_ptr<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>>
        m_Statistics =
            std::make_shared<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>>();

    // The original counts, counted on a worker one count at a time; a request while one runs is taken up when it lands.
    std::shared_ptr<const an::ui::HistogramDisplayData> m_OriginalCounts;
    bool m_CountRunning = false;
    bool m_CountPending = false;

    // The preview counts, remapped on a worker the same way.
    bool m_RemapRunning = false;
    bool m_RemapPending = false;
};
